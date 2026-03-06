#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/wifi-module.h"

#include <memory>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("UhrAp5ClientsTcp");

int
main(int argc, char* argv[])
{
    uint32_t nStas = 10;
    double simTime = 10.0;
    double appStart = 1.0;
    uint32_t payloadSize = 1448; // Typical MSS-sized payload
    uint8_t ehtMcs = 7;
    uint16_t channelWidth = 80;
    bool enablePcap = true;
    bool enableAnim = true;
    std::string pcapPrefix = "scratch/uhr-ap-5-clients-tcp";
    std::string animFile = "scratch/uhr-ap-5-clients-tcp.xml";

    CommandLine cmd(__FILE__);
    cmd.AddValue("nStas", "Number of UHR clients", nStas);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("appStart", "Application start time in seconds", appStart);
    cmd.AddValue("payloadSize", "TCP payload size in bytes", payloadSize);
    cmd.AddValue("ehtMcs", "EHT/UHR MCS index", ehtMcs);
    cmd.AddValue("channelWidth", "Channel width in MHz", channelWidth);
    cmd.AddValue("enablePcap", "Enable pcap output", enablePcap);
    cmd.AddValue("enableAnim", "Enable NetAnim XML output", enableAnim);
    cmd.AddValue("pcapPrefix", "PCAP prefix/path", pcapPrefix);
    cmd.AddValue("animFile", "NetAnim XML file path", animFile);
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(payloadSize));

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
    pos->Add(Vector(0.0, 0.0, 0.0)); // AP
    for (uint32_t i = 0; i < nStas; ++i)
    {
        pos->Add(Vector(3.0 + 2.0 * i, 2.0 + i, 0.0));
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
    for (uint32_t i = 0; i < nStas; ++i)
    {
        const uint16_t port = 6000 + i;
        Address sinkAddr(InetSocketAddress(Ipv4Address::GetAny(), port));
        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", sinkAddr);
        auto sink = sinkHelper.Install(apNode.Get(0));
        sink.Start(Seconds(0.5));
        sink.Stop(Seconds(simTime));
        sinkApps.Add(sink);

        BulkSendHelper sourceHelper("ns3::TcpSocketFactory", InetSocketAddress(apAddr, port));
        sourceHelper.SetAttribute("MaxBytes", UintegerValue(0)); // Unlimited until stop time
        sourceHelper.SetAttribute("Tos", UintegerValue(0xb8));   // Map traffic to AC_VO
        auto src = sourceHelper.Install(staNodes.Get(i));
        src.Start(Seconds(appStart));
        src.Stop(Seconds(simTime));
        sourceApps.Add(src);
    }

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

    Simulator::Destroy();
    return 0;
}
