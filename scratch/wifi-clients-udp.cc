#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiEdca10ClientsUdp");

namespace
{
struct AccessCategoryConfig
{
    const char* name;
    uint8_t tos;
    double rateMbps;
};

struct FlowResult
{
    uint32_t staIndex;
    std::string accessCategory;
    uint8_t tos;
    uint64_t rxBytes;
    double throughputMbps;
};

std::string
HexByte(uint8_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << static_cast<uint32_t>(value);
    return oss.str();
}

class DynamicEdcaStaApp : public Application
{
  public:
    static TypeId GetTypeId();

    DynamicEdcaStaApp(std::array<AccessCategoryConfig, 4> acConfigs,
                      Ipv4Address apAddress,
                      std::array<uint16_t, 4> ports,
                      uint32_t packetSize,
                      double meanProfileMs,
                      double probabilityTwoFlows)
        : m_acConfigs(std::move(acConfigs)),
          m_apAddress(apAddress),
          m_ports(ports),
          m_packetSize(packetSize),
          m_meanProfileMs(meanProfileMs),
          m_probabilityTwoFlows(probabilityTwoFlows)
    {
    }

  private:
    void StartApplication() override;
    void StopApplication() override;
    void ChangeProfile();
    void ActivateFlow(uint32_t acIdx);
    void ScheduleNextPacket(uint32_t acIdx);
    void SendPacket(uint32_t acIdx);
    uint32_t PickAccessCategory(int exclude = -1) const;

    std::array<AccessCategoryConfig, 4> m_acConfigs;
    Ipv4Address m_apAddress;
    std::array<uint16_t, 4> m_ports;
    uint32_t m_packetSize;
    double m_meanProfileMs;
    double m_probabilityTwoFlows;
    std::array<Ptr<Socket>, 4> m_sockets;
    std::array<EventId, 4> m_sendEvents;
    std::array<bool, 4> m_isActive{{false, false, false, false}};
    EventId m_profileEvent;
    Ptr<UniformRandomVariable> m_uniform = CreateObject<UniformRandomVariable>();
    Ptr<ExponentialRandomVariable> m_profileDurationRv = CreateObject<ExponentialRandomVariable>();
};

NS_OBJECT_ENSURE_REGISTERED(DynamicEdcaStaApp);

TypeId
DynamicEdcaStaApp::GetTypeId()
{
    static TypeId tid = TypeId("ns3::DynamicEdcaStaApp").SetParent<Application>();
    return tid;
}

void
DynamicEdcaStaApp::StartApplication()
{
    m_profileDurationRv->SetAttribute("Mean", DoubleValue(m_meanProfileMs));
    for (uint32_t acIdx = 0; acIdx < m_acConfigs.size(); ++acIdx)
    {
        auto socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        socket->SetIpTos(m_acConfigs[acIdx].tos);
        socket->Connect(InetSocketAddress(m_apAddress, m_ports[acIdx]));
        m_sockets[acIdx] = socket;
    }
    // As soon as the app starts, choose the first traffic profile for this STA.
    ChangeProfile();
}

void
DynamicEdcaStaApp::StopApplication()
{
    if (m_profileEvent.IsPending())
    {
        Simulator::Cancel(m_profileEvent);
    }
    for (uint32_t acIdx = 0; acIdx < m_sendEvents.size(); ++acIdx)
    {
        if (m_sendEvents[acIdx].IsPending())
        {
            Simulator::Cancel(m_sendEvents[acIdx]);
        }
        if (m_sockets[acIdx])
        {
            m_sockets[acIdx]->Close();
            m_sockets[acIdx] = nullptr;
        }
        m_isActive[acIdx] = false;
    }
}

uint32_t
DynamicEdcaStaApp::PickAccessCategory(int exclude) const
{
    // Simple weighted choice: BE is common, VI is common, VO/BK are rarer.
    // This is where the next candidate AC is randomly selected.
    // This weight is responsible for picking a certain AC
    std::array<double, 4> weights = {0.40, 0.10, 0.35, 0.15};
    if (exclude >= 0)
    {
        weights[exclude] = 0.0;
    }
    const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
    const double sample = m_uniform->GetValue(0.0, total);
    double running = 0.0;
    for (uint32_t i = 0; i < weights.size(); ++i)
    {
        running += weights[i];
        if (sample <= running)
        {
            return i;
        }
    }
    return 0;
}

void
DynamicEdcaStaApp::ChangeProfile()
{
    // STEP 1:
    // Forget the old profile for this STA.
    // If BE or VI or VO was active before, we stop it here.
    // This is the part where the STA "lets go" of its previous AC(s).
    for (uint32_t acIdx = 0; acIdx < m_isActive.size(); ++acIdx)
    {
        m_isActive[acIdx] = false;
        if (m_sendEvents[acIdx].IsPending())
        {
            Simulator::Cancel(m_sendEvents[acIdx]);
        }
    }

    // STEP 2:
    // Decide how many ACs this STA should have right now.
    // Sometimes it has only one active AC, sometimes two active ACs.
    const bool useTwoFlows = m_uniform->GetValue() < m_probabilityTwoFlows;

    // STEP 3:
    // Randomly choose the first active AC for this STA.
    const uint32_t primary = PickAccessCategory();
    ActivateFlow(primary);

    if (useTwoFlows)
    {
        // STEP 4:
        // If this profile uses two ACs, randomly choose a second one that is
        // different from the first.
        const uint32_t secondary = PickAccessCategory(static_cast<int>(primary));
        ActivateFlow(secondary);
    }

    // STEP 5:
    // Stay with this profile for a while, then come back here and choose a new one.
    m_profileEvent =
        Simulator::Schedule(MilliSeconds(m_profileDurationRv->GetValue()),
                            &DynamicEdcaStaApp::ChangeProfile,
                            this);
}

void
DynamicEdcaStaApp::ActivateFlow(uint32_t acIdx)
{
    // This AC is now part of the STA's current traffic profile.
    // Example: if acIdx corresponds to VI, this STA is now generating VI traffic.
    m_isActive[acIdx] = true;
    SendPacket(acIdx);
}

void
DynamicEdcaStaApp::ScheduleNextPacket(uint32_t acIdx)
{
    // Packet spacing is derived from the configured offered load for this AC.
    const double intervalSeconds =
        (m_packetSize * 8.0) / (m_acConfigs[acIdx].rateMbps * 1000000.0);
    m_sendEvents[acIdx] =
        Simulator::Schedule(Seconds(intervalSeconds), &DynamicEdcaStaApp::SendPacket, this, acIdx);
}

void
DynamicEdcaStaApp::SendPacket(uint32_t acIdx)
{
    // Only send if this AC is still active in the current profile.
    // If the STA already switched to a new profile, this old AC stops here.
    if (!m_isActive[acIdx] || !m_sockets[acIdx])
    {
        return;
    }
    // The socket already carries the TOS value for this AC, so every packet
    // sent here is queued into the matching EDCA access category by ns-3.
    m_sockets[acIdx]->Send(Create<Packet>(m_packetSize));
    ScheduleNextPacket(acIdx);
}
} // namespace

int
main(int argc, char* argv[])
{
    // Basic simulation controls.
    uint32_t nClients = 10;
    double simTime = 10.0;
    double appStart = 5.0;
    uint32_t packetSize = 1200;
    double radiusMeters = 5.0;

    // Per-STA offered load for each EDCA category.
    double beRateMbps = 5.0;
    double bkRateMbps = 2.0;
    double viRateMbps = 10.0;
    double voRateMbps = 3.0;
    double meanProfileMs = 250.0;
    double probabilityTwoFlows = 0.15;

    bool enablePcap = false;
    std::string pcapPrefix = "scratch/wifi-edca-10-clients-udp";

    CommandLine cmd(__FILE__);
    cmd.AddValue("nClients", "Number of Wi-Fi clients", nClients);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("appStart", "Application start time in seconds", appStart);
    cmd.AddValue("packetSize", "UDP payload size in bytes", packetSize);
    cmd.AddValue("radiusMeters", "Radius of the client ring around the AP", radiusMeters);
    cmd.AddValue("beRateMbps", "Per-STA offered load for AC_BE in Mbit/s", beRateMbps);
    cmd.AddValue("bkRateMbps", "Per-STA offered load for AC_BK in Mbit/s", bkRateMbps);
    cmd.AddValue("viRateMbps", "Per-STA offered load for AC_VI in Mbit/s", viRateMbps);
    cmd.AddValue("voRateMbps", "Per-STA offered load for AC_VO in Mbit/s", voRateMbps);
    cmd.AddValue("meanProfileMs",
                 "Mean time before a STA picks a new traffic profile in ms",
                 meanProfileMs);
    cmd.AddValue("probabilityTwoFlows",
                 "Probability that a STA has two simultaneous ACs instead of one",
                 probabilityTwoFlows);
    cmd.AddValue("enablePcap", "Enable pcap tracing", enablePcap);
    cmd.AddValue("pcapPrefix", "Prefix for pcap files", pcapPrefix);
    cmd.Parse(argc, argv);

    const double activeTime = std::max(0.001, simTime - appStart);
    // Each STA can dynamically switch between profiles where one or two
    // access categories are active at a time.
    const std::array<AccessCategoryConfig, 4> acConfigs = {{
        {"BE", 0x70, beRateMbps},
        {"BK", 0x28, bkRateMbps},
        {"VI", 0xb8, viRateMbps},
        {"VO", 0xc0, voRateMbps},
    }};

    // Create 10 Wi-Fi stations and one access point.
    NodeContainer staNodes;
    staNodes.Create(nClients);
    NodeContainer apNode;
    apNode.Create(1);

    // Build a simple Yans Wi-Fi channel/PHY pair.
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    // Use a fixed 802.11n data/control rate so EDCA effects are easier to see.
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("HtMcs7"),
                                 "ControlMode",
                                 StringValue("HtMcs0"));

    WifiMacHelper mac;
    Ssid ssid("edca-demo");

    // Install QoS-capable station MACs so packets can be queued per access category.
    mac.SetType("ns3::StaWifiMac",
                "Ssid",
                SsidValue(ssid),
                "QosSupported",
                BooleanValue(true));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, staNodes);

    // Install the AP with QoS enabled as well.
    mac.SetType("ns3::ApWifiMac",
                "Ssid",
                SsidValue(ssid),
                "QosSupported",
                BooleanValue(true),
                "EnableBeaconJitter",
                BooleanValue(false));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, apNode);

    Config::Set("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/ChannelSettings",
                StringValue("{0, 20, BAND_5GHZ, 0}"));

    // Put the AP at the center and place STAs around it on a small ring.
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));
    for (uint32_t i = 0; i < nClients; ++i)
    {
        const double angle = 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(nClients);
        positionAlloc->Add(Vector(radiusMeters * std::cos(angle), radiusMeters * std::sin(angle), 0.0));
    }
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNode);
    mobility.Install(staNodes);

    // Install IP so the UDP applications can communicate over Wi-Fi.
    InternetStackHelper internet;
    internet.Install(apNode);
    internet.Install(staNodes);

    // Put every node into the same subnet.
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer staIfaces = ipv4.Assign(staDevices);
    Ipv4InterfaceContainer apIface = ipv4.Assign(apDevice);

    ApplicationContainer sinkApps;
    std::vector<Ptr<PacketSink>> sinks;
    std::vector<FlowResult> results;
    ApplicationContainer dynamicApps;

    const auto apAddress = apIface.GetAddress(0);
    uint16_t nextPort = 9000;

    for (uint32_t staIdx = 0; staIdx < nClients; ++staIdx)
    {
        std::array<uint16_t, 4> staPorts{};
        for (uint32_t acIdx = 0; acIdx < acConfigs.size(); ++acIdx)
        {
            const auto& ac = acConfigs[acIdx];
            // Give every (STA, AC) flow its own UDP destination port so we can
            // measure received bytes separately at the AP.
            const uint16_t port = nextPort++;
            staPorts[acIdx] = port;

            PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                        InetSocketAddress(Ipv4Address::GetAny(), port));
            ApplicationContainer installedSink = sinkHelper.Install(apNode.Get(0));
            sinkApps.Add(installedSink);
            sinks.push_back(DynamicCast<PacketSink>(installedSink.Get(0)));
            results.push_back({staIdx, ac.name, ac.tos, 0, 0.0});
        }

        // Install one dynamic traffic app on this STA.
        // That app is responsible for randomly choosing which AC(s) are active over time.
        Ptr<DynamicEdcaStaApp> app = CreateObject<DynamicEdcaStaApp>(acConfigs,
                                                                     apAddress,
                                                                     staPorts,
                                                                     packetSize,
                                                                     meanProfileMs,
                                                                     probabilityTwoFlows);
        staNodes.Get(staIdx)->AddApplication(app);
        app->SetStartTime(Seconds(appStart + 0.001 * staIdx));
        app->SetStopTime(Seconds(simTime));
        dynamicApps.Add(app);
    }

    // Start sinks first so they are ready before sources begin sending.
    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(Seconds(simTime));

    if (enablePcap)
    {
        phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
        phy.EnablePcap(pcapPrefix + "-ap", apDevice.Get(0), true);
        phy.EnablePcap(pcapPrefix + "-sta", staDevices, true);
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Run the simulation and then pull receive counters from each AP sink.
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    std::map<std::string, double> throughputByAc;
    std::map<std::string, uint64_t> bytesByAc;
    std::vector<double> throughputBySta(nClients, 0.0);
    std::vector<uint64_t> bytesBySta(nClients, 0);
    double totalThroughputMbps = 0.0;

    for (uint32_t i = 0; i < results.size(); ++i)
    {
        results[i].rxBytes = sinks[i]->GetTotalRx();
        results[i].throughputMbps = (results[i].rxBytes * 8.0) / (activeTime * 1e6);
        totalThroughputMbps += results[i].throughputMbps;
        // Keep both per-AC and per-STA summaries so we can inspect
        // prioritization and fairness from the same run.
        throughputByAc[results[i].accessCategory] += results[i].throughputMbps;
        bytesByAc[results[i].accessCategory] += results[i].rxBytes;
        throughputBySta[results[i].staIndex] += results[i].throughputMbps;
        bytesBySta[results[i].staIndex] += results[i].rxBytes;
    }

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Wi-Fi EDCA UDP demo\n";
    std::cout << "  AP address:            " << apAddress << "\n";
    std::cout << "  Clients:               " << nClients << "\n";
    std::cout << "  Packet size:           " << packetSize << " bytes\n";
    std::cout << "  Active interval:       " << activeTime << " s\n";
    std::cout << "  Total throughput:      " << totalThroughputMbps << " Mbit/s\n";
    std::cout << "  Offered per STA:       BE=" << beRateMbps << " BK=" << bkRateMbps
              << " VI=" << viRateMbps << " VO=" << voRateMbps << " Mbit/s\n";
    std::cout << "  Dynamic profile model: meanProfile=" << meanProfileMs
              << " ms, two-flow probability=" << probabilityTwoFlows << "\n";

    std::cout << "\nPer-STA totals\n";
    for (uint32_t staIdx = 0; staIdx < nClients; ++staIdx)
    {
        std::cout << "  STA " << std::setw(2) << staIdx << " (IP " << staIfaces.GetAddress(staIdx)
                  << ")  rx=" << bytesBySta[staIdx] << " bytes  thr=" << throughputBySta[staIdx]
                  << " Mbit/s\n";
    }

    std::cout << "\nPer-STA per-AC flows\n";
    for (const auto& result : results)
    {
        std::cout << "  STA " << std::setw(2) << result.staIndex << " AC_" << result.accessCategory
                  << " (TOS " << HexByte(result.tos) << ")  rx=" << result.rxBytes
                  << " bytes  thr=" << result.throughputMbps << " Mbit/s\n";
    }

    std::cout << "\nPer-access-category totals\n";
    for (const auto& ac : acConfigs)
    {
        std::cout << "  AC_" << ac.name << "  rx=" << bytesByAc[ac.name]
                  << " bytes  thr=" << throughputByAc[ac.name] << " Mbit/s\n";
    }

    Simulator::Destroy();
    return 0;
}
