#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/wifi-module.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("UhrAp5ClientsTcp");

namespace
{
struct AccessStats
{
    std::vector<std::map<uint8_t, double>> lastBackoffStartByLink;
    std::vector<double> accessDelayMs;
};

void
OnBackoff(AccessStats* stats, uint32_t idx, uint32_t slots, uint8_t linkId)
{
    (void)slots;
    stats->lastBackoffStartByLink[idx][linkId] = Simulator::Now().GetSeconds();
}

void
OnTxop(AccessStats* stats, uint32_t idx, Time start, Time duration, uint8_t linkId)
{
    (void)duration;
    if (auto it = stats->lastBackoffStartByLink[idx].find(linkId);
        it != stats->lastBackoffStartByLink[idx].end())
    {
        stats->accessDelayMs.push_back((start.GetSeconds() - it->second) * 1000.0);
    }
}

std::vector<std::pair<double, double>>
BuildCdfFromValues(std::vector<double> values)
{
    std::vector<std::pair<double, double>> cdf;
    if (values.empty())
    {
        return cdf;
    }
    std::sort(values.begin(), values.end());
    cdf.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        cdf.emplace_back(values[i], static_cast<double>(i + 1) / static_cast<double>(values.size()));
    }
    return cdf;
}

void
WriteCdfCsv(const std::string& path, const std::vector<std::pair<double, double>>& cdf)
{
    std::ofstream out(path);
    out << "value_ms,cdf\n";
    out << std::fixed << std::setprecision(6);
    for (const auto& [value, prob] : cdf)
    {
        out << value << "," << prob << "\n";
    }
}
} // namespace

int
main(int argc, char* argv[])
{
    uint32_t nStas = 5;
    double simTime = 10.0;
    double appStart = 1.0;
    uint32_t payloadSize = 1448; // Typical MSS-sized payload
    uint8_t ehtMcs = 0;
    uint16_t channelWidth = 20;
    uint8_t nss = 2;
    double txopMinMs = 1.5;
    double txopMaxMs = 5.0;
    bool enablePcap = true;
    bool enableAnim = true;
    std::string pcapPrefix = "scratch/uhr-ap-5-clients-tcp";
    std::string animFile = "scratch/uhr-ap-5-clients-tcp.xml";
    std::string accessPrefix = "scratch/uhr-ap-5-clients-tcp-access";

    CommandLine cmd(__FILE__);
    cmd.AddValue("nStas", "Number of UHR clients", nStas);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("appStart", "Application start time in seconds", appStart);
    cmd.AddValue("payloadSize", "TCP payload size in bytes", payloadSize);
    cmd.AddValue("ehtMcs", "EHT/UHR MCS index", ehtMcs);
    cmd.AddValue("channelWidth", "Channel width in MHz", channelWidth);
    cmd.AddValue("nss", "Number of spatial streams", nss);
    cmd.AddValue("txopMinMs", "Minimum TXOP limit (ms)", txopMinMs);
    cmd.AddValue("txopMaxMs", "Maximum TXOP limit (ms)", txopMaxMs);
    cmd.AddValue("enablePcap", "Enable pcap output", enablePcap);
    cmd.AddValue("enableAnim", "Enable NetAnim XML output", enableAnim);
    cmd.AddValue("pcapPrefix", "PCAP prefix/path", pcapPrefix);
    cmd.AddValue("animFile", "NetAnim XML file path", animFile);
    cmd.AddValue("accessPrefix", "Prefix for channel access CDF outputs", accessPrefix);
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(payloadSize));
    Config::SetDefault("ns3::QosTxop::RandomTxopLimitEnabled", BooleanValue(true));
    Config::SetDefault("ns3::QosTxop::RandomTxopLimitMin",
                       TimeValue(MicroSeconds(static_cast<uint64_t>(txopMinMs * 1000.0))));
    Config::SetDefault("ns3::QosTxop::RandomTxopLimitMax",
                       TimeValue(MicroSeconds(static_cast<uint64_t>(txopMaxMs * 1000.0))));

    NodeContainer staNodes;
    NodeContainer apNode;
    staNodes.Create(nStas);
    apNode.Create(1);

    WifiHelper wifi;
    wifi.SetStandard("UHR");
    const std::string dataMode = "EhtMcs" + std::to_string(ehtMcs);
    const uint64_t nonHtRefRateMbps = EhtPhy::GetNonHtReferenceRate(ehtMcs) / 1000000;
    const std::string controlMode = "OfdmRate" + std::to_string(nonHtRefRateMbps) + "Mbps";
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue(dataMode),
                                 "ControlMode",
                                 StringValue(controlMode),
                                 "RtsCtsThreshold",
                                 UintegerValue(0));

    SpectrumWifiPhyHelper phy(1);
    const std::string channelSettings =
        "{0, " + std::to_string(channelWidth) + ", BAND_5GHZ, 0}";
    phy.Set(0, "ChannelSettings", StringValue(channelSettings));
    phy.Set("Antennas", UintegerValue(nss));
    phy.Set("MaxSupportedTxSpatialStreams", UintegerValue(nss));
    phy.Set("MaxSupportedRxSpatialStreams", UintegerValue(nss));
    phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
    auto spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
    spectrumChannel->AddPropagationLossModel(CreateObject<LogDistancePropagationLossModel>());
    phy.AddChannel(spectrumChannel, WIFI_SPECTRUM_5_GHZ);

    WifiMacHelper mac;
    Ssid ssid("uhr-ap-5-clients");

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, staNodes);

    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, apNode);

    if (enablePcap)
    {
        phy.EnablePcap(pcapPrefix + "-ap", apDevice.Get(0));
        phy.EnablePcap(pcapPrefix + "-sta", staDevices);
    }

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
    const double boxX = 12.5;
    const double boxY = 12.5;
    const double boxZ = 3.0;
    pos->Add(Vector(boxX / 2.0, boxY / 2.0, boxZ / 2.0)); // AP in the middle
    auto randX = CreateObject<UniformRandomVariable>();
    randX->SetAttribute("Min", DoubleValue(0.0));
    randX->SetAttribute("Max", DoubleValue(boxX));
    auto randY = CreateObject<UniformRandomVariable>();
    randY->SetAttribute("Min", DoubleValue(0.0));
    randY->SetAttribute("Max", DoubleValue(boxY));
    auto randZ = CreateObject<UniformRandomVariable>();
    randZ->SetAttribute("Min", DoubleValue(0.0));
    randZ->SetAttribute("Max", DoubleValue(boxZ));
    for (uint32_t i = 0; i < nStas; ++i)
    {
        pos->Add(Vector(randX->GetValue(), randY->GetValue(), randZ->GetValue()));
    }
    mobility.SetPositionAllocator(pos);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNode);
    mobility.Install(staNodes);

    InternetStackHelper internet;
    internet.Install(apNode);
    internet.Install(staNodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    NetDeviceContainer allDevices;
    allDevices.Add(apDevice);
    allDevices.Add(staDevices);
    Ipv4InterfaceContainer ifaces = ipv4.Assign(allDevices);
    const Ipv4Address apAddr = ifaces.GetAddress(0);

    ApplicationContainer sinkApps;
    ApplicationContainer sourceApps;
    ApplicationContainer dlSinkApps;
    ApplicationContainer dlSourceApps;
    for (uint32_t i = 0; i < nStas; ++i)
    {
        const uint16_t port = 6000 + i;
        Address sinkAddr(InetSocketAddress(Ipv4Address::GetAny(), port));
        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", sinkAddr);
        auto sink = sinkHelper.Install(apNode.Get(0));
        sink.Start(Seconds(0.0));
        sink.Stop(Seconds(simTime));
        sinkApps.Add(sink);

        BulkSendHelper sourceHelper("ns3::TcpSocketFactory", InetSocketAddress(apAddr, port));
        sourceHelper.SetAttribute("MaxBytes", UintegerValue(0)); // Unlimited until stop time
        sourceHelper.SetAttribute("Tos", UintegerValue(0xb8));   // Map traffic to AC_VO
        auto src = sourceHelper.Install(staNodes.Get(i));
        src.Start(Seconds(appStart));
        src.Stop(Seconds(simTime));
        sourceApps.Add(src);

        const uint16_t dlPort = 7000 + i;
        Address dlSinkAddr(InetSocketAddress(Ipv4Address::GetAny(), dlPort));
        PacketSinkHelper dlSinkHelper("ns3::TcpSocketFactory", dlSinkAddr);
        auto dlSink = dlSinkHelper.Install(staNodes.Get(i));
        dlSink.Start(Seconds(0.0));
        dlSink.Stop(Seconds(simTime));
        dlSinkApps.Add(dlSink);

        BulkSendHelper dlSourceHelper("ns3::TcpSocketFactory",
                                      InetSocketAddress(ifaces.GetAddress(i + 1), dlPort));
        dlSourceHelper.SetAttribute("MaxBytes", UintegerValue(0));
        dlSourceHelper.SetAttribute("Tos", UintegerValue(0xb8));
        auto dlSrc = dlSourceHelper.Install(apNode.Get(0));
        dlSrc.Start(Seconds(appStart));
        dlSrc.Stop(Seconds(simTime));
        dlSourceApps.Add(dlSrc);
    }

    AccessStats ulAccessStats;
    AccessStats dlAccessStats;
    ulAccessStats.lastBackoffStartByLink.resize(nStas);
    dlAccessStats.lastBackoffStartByLink.resize(1);
    for (uint32_t i = 0; i < nStas; ++i)
    {
        auto staNetDev = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        NS_ASSERT(staNetDev);
        auto staMac = staNetDev->GetMac();
        auto voTxop = staMac->GetQosTxop(AC_VO);
        voTxop->TraceConnectWithoutContext("BackoffTrace",
                                           MakeBoundCallback(&OnBackoff, &ulAccessStats, i));
        voTxop->TraceConnectWithoutContext("TxopTrace",
                                           MakeBoundCallback(&OnTxop, &ulAccessStats, i));
    }
    auto apNetDev = DynamicCast<WifiNetDevice>(apDevice.Get(0));
    NS_ASSERT(apNetDev);
    auto apMac = apNetDev->GetMac();
    auto apVoTxop = apMac->GetQosTxop(AC_VO);
    apVoTxop->TraceConnectWithoutContext("BackoffTrace",
                                         MakeBoundCallback(&OnBackoff, &dlAccessStats, 0));
    apVoTxop->TraceConnectWithoutContext("TxopTrace",
                                         MakeBoundCallback(&OnTxop, &dlAccessStats, 0));

    std::unique_ptr<AnimationInterface> anim;
    if (enableAnim)
    {
        anim = std::make_unique<AnimationInterface>(animFile);
    }

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    uint64_t totalRx = 0;
    for (uint32_t i = 0; i < sinkApps.GetN(); ++i)
    {
        auto sink = DynamicCast<PacketSink>(sinkApps.Get(i));
        const uint64_t rx = sink->GetTotalRx();
        totalRx += rx;
        std::cout << "STA-" << i << " -> AP bytes: " << rx << "\n";
    }

    const double active = std::max(0.001, simTime - appStart);
    const double throughputMbps = (totalRx * 8.0) / (active * 1e6);
    std::cout << "UHR AP+" << nStas << " Clients TCP summary\n";
    std::cout << "  AP IP: " << apAddr << "\n";
    std::cout << "  Data mode: " << dataMode << ", channel width: " << channelWidth << " MHz\n";
    std::cout << "  Total RX bytes at AP: " << totalRx << "\n";
    std::cout << "  Aggregate throughput: " << throughputMbps << " Mbit/s\n";

    const auto ulAccessCdf = BuildCdfFromValues(ulAccessStats.accessDelayMs);
    const auto dlAccessCdf = BuildCdfFromValues(dlAccessStats.accessDelayMs);
    WriteCdfCsv(accessPrefix + "-ul-cdf.csv", ulAccessCdf);
    WriteCdfCsv(accessPrefix + "-dl-cdf.csv", dlAccessCdf);

    std::ofstream plt(accessPrefix + "-cdf.plt");
    plt << "set terminal pngcairo size 1200,800\n";
    plt << "set datafile separator ','\n";
    plt << "set output '" << accessPrefix << "-cdf.png'\n";
    plt << "set title 'Channel Access Time CDF (UL vs DL)'\n";
    plt << "set xlabel 'Channel Access Time (ms)'\n";
    plt << "set ylabel 'CDF'\n";
    plt << "set key left bottom\n";
    plt << "set grid\n";
    plt << "plot '" << accessPrefix
        << "-ul-cdf.csv' using 1:2 with lines lw 2 title 'UL',\\\n";
    plt << "     '" << accessPrefix
        << "-dl-cdf.csv' using 1:2 with lines lw 2 title 'DL'\n";

    if (ulAccessStats.accessDelayMs.empty())
    {
        std::cout << "  WARNING: No UL channel access samples collected\\n";
    }
    if (dlAccessStats.accessDelayMs.empty())
    {
        std::cout << "  WARNING: No DL channel access samples collected\\n";
    }
    std::cout << "  Channel access CDF outputs: " << accessPrefix << "-ul-cdf.csv, "
              << accessPrefix << "-dl-cdf.csv, " << accessPrefix << "-cdf.plt\\n";

    Simulator::Destroy();
    return 0;
}
