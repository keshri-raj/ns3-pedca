#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/eht-phy.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/neighbor-cache-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/wifi-module.h"
#include "ns3/wifi-static-setup-helper.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("UhrMldAp10ClientsVoTcp");

namespace
{
struct Percentiles
{
    double p50{0.0};
    double p90{0.0};
    double p95{0.0};
    double p99{0.0};
    double p999{0.0};
};

double
GetPercentileFromSorted(const std::vector<double>& sortedValues, double q)
{
    if (sortedValues.empty())
    {
        return 0.0;
    }
    const auto idx =
        static_cast<std::size_t>(std::ceil(q * static_cast<double>(sortedValues.size()))) - 1;
    return sortedValues[std::min(idx, sortedValues.size() - 1)];
}

Percentiles
GetPercentiles(std::vector<double> values)
{
    if (values.empty())
    {
        return {};
    }
    std::sort(values.begin(), values.end());
    return {GetPercentileFromSorted(values, 0.50),
            GetPercentileFromSorted(values, 0.90),
            GetPercentileFromSorted(values, 0.95),
            GetPercentileFromSorted(values, 0.99),
            GetPercentileFromSorted(values, 0.999)};
}

Percentiles
GetHistogramPercentiles(const std::map<double, uint64_t>& bins, uint64_t totalCount)
{
    if (totalCount == 0 || bins.empty())
    {
        return {};
    }
    const auto percentile = [&](double q) {
        const auto target = static_cast<uint64_t>(std::ceil(q * static_cast<double>(totalCount)));
        uint64_t cumulative = 0;
        for (const auto& [value, count] : bins)
        {
            cumulative += count;
            if (cumulative >= std::max<uint64_t>(1, target))
            {
                return value;
            }
        }
        return bins.rbegin()->first;
    };
    return {percentile(0.50),
            percentile(0.90),
            percentile(0.95),
            percentile(0.99),
            percentile(0.999)};
}

std::vector<std::pair<double, double>>
BuildCdf(const std::map<double, uint64_t>& bins, uint64_t totalCount)
{
    std::vector<std::pair<double, double>> cdf;
    if (totalCount == 0 || bins.empty())
    {
        return cdf;
    }
    uint64_t cumulative = 0;
    cdf.reserve(bins.size());
    for (const auto& [value, count] : bins)
    {
        cumulative += count;
        cdf.emplace_back(value, static_cast<double>(cumulative) / static_cast<double>(totalCount));
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
    uint32_t nStas = 10;
    uint32_t nLinks = 2;
    double simTime = 10.0;
    double appStart = 1.0;
    uint32_t payloadSize = 1448;
    uint8_t ehtMcs = 7;
    uint16_t channelWidth = 80;
    bool enablePcap = true;
    bool enableAnim = true;
    bool useStaticSetup = false;
    std::string pcapPrefix = "scratch/uhr-mld-ap-10-clients-vo-tcp";
    std::string animFile = "scratch/uhr-mld-ap-10-clients-vo-tcp.xml";
    std::string tailPrefix = "scratch/uhr-mld-ap-10-clients-vo-tcp-tail";

    CommandLine cmd(__FILE__);
    cmd.AddValue("nStas", "Number of UHR MLD clients", nStas);
    cmd.AddValue("nLinks", "Number of links per MLD (1-2)", nLinks);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("appStart", "Application start time in seconds", appStart);
    cmd.AddValue("payloadSize", "TCP payload size in bytes", payloadSize);
    cmd.AddValue("ehtMcs", "EHT/UHR MCS index", ehtMcs);
    cmd.AddValue("channelWidth", "Channel width in MHz", channelWidth);
    cmd.AddValue("enablePcap", "Enable pcap output", enablePcap);
    cmd.AddValue("enableAnim", "Enable NetAnim XML output", enableAnim);
    cmd.AddValue("useStaticSetup", "Use static association/ARP setup", useStaticSetup);
    cmd.AddValue("pcapPrefix", "PCAP prefix/path", pcapPrefix);
    cmd.AddValue("animFile", "NetAnim XML file path", animFile);
    cmd.AddValue("tailPrefix", "Prefix for tail-latency CSV/plot outputs", tailPrefix);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(nLinks < 1 || nLinks > 2, "nLinks must be 1 or 2");

    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(payloadSize));

    NodeContainer staNodes;
    NodeContainer apNode;
    staNodes.Create(nStas);
    apNode.Create(1);

    WifiHelper wifi;
    wifi.SetStandard("UHR");

    const std::string dataMode = "EhtMcs" + std::to_string(ehtMcs);
    const uint64_t nonHtRefRateMbps = EhtPhy::GetNonHtReferenceRate(ehtMcs) / 1000000;
    const std::string controlMode5 = "OfdmRate" + std::to_string(nonHtRefRateMbps) + "Mbps";

    wifi.SetRemoteStationManager(static_cast<uint8_t>(0),
                                 "ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue(dataMode),
                                 "ControlMode",
                                 StringValue(controlMode5),
                                 "RtsCtsThreshold",
                                 UintegerValue(0));

    if (nLinks == 2)
    {
        wifi.SetRemoteStationManager(static_cast<uint8_t>(1),
                                     "ns3::ConstantRateWifiManager",
                                     "DataMode",
                                     StringValue(dataMode),
                                     "ControlMode",
                                     StringValue(controlMode5),
                                     "RtsCtsThreshold",
                                     UintegerValue(0));
    }

    SpectrumWifiPhyHelper phy(nLinks);
    phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    std::array<std::string, 2> channelStr = {
        "{0, " + std::to_string(channelWidth) + ", BAND_5GHZ, 0}",
        "{0, " + std::to_string(channelWidth) + ", BAND_6GHZ, 0}"};
    std::array<FrequencyRange, 2> freqRanges = {WIFI_SPECTRUM_5_GHZ, WIFI_SPECTRUM_6_GHZ};

    for (uint8_t linkId = 0; linkId < nLinks; ++linkId)
    {
        phy.Set(linkId, "ChannelSettings", StringValue(channelStr[linkId]));
        auto spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
        spectrumChannel->AddPropagationLossModel(CreateObject<LogDistancePropagationLossModel>());
        phy.AddChannel(spectrumChannel, freqRanges[linkId]);
    }

    WifiMacHelper mac;
    Ssid ssid("uhr-mld-ap-clients");

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, staNodes);

    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid), "EnableBeaconJitter", BooleanValue(false));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, apNode);

    if (enablePcap)
    {
        phy.EnablePcap(pcapPrefix + "-ap", apDevice.Get(0));
        phy.EnablePcap(pcapPrefix + "-sta", staDevices);
    }

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
    pos->Add(Vector(0.0, 0.0, 0.0));
    for (uint32_t i = 0; i < nStas; ++i)
    {
        pos->Add(Vector(3.0 + 2.0 * i, 2.0 + i, 0.0));
    }
    mobility.SetPositionAllocator(pos);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNode);
    mobility.Install(staNodes);

    if (useStaticSetup)
    {
        auto apDev = DynamicCast<WifiNetDevice>(apDevice.Get(0));
        NS_ASSERT(apDev);
        WifiStaticSetupHelper::SetStaticAssociation(apDev, staDevices);
        WifiStaticSetupHelper::SetStaticBlockAck(apDev, staDevices, {6, 7});
        if (nLinks > 1)
        {
            WifiStaticSetupHelper::SetStaticEmlsr(apDev, staDevices);
        }
        appStart = 0.1;
    }

    InternetStackHelper internet;
    internet.Install(apNode);
    internet.Install(staNodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.3.0", "255.255.255.0");
    NetDeviceContainer allDevices;
    allDevices.Add(apDevice);
    allDevices.Add(staDevices);
    Ipv4InterfaceContainer ifaces = ipv4.Assign(allDevices);
    const Ipv4Address apAddr = ifaces.GetAddress(0);

    if (useStaticSetup)
    {
        NeighborCacheHelper nbCache;
        nbCache.PopulateNeighborCache();
    }

    ApplicationContainer sinkApps;
    ApplicationContainer sourceApps;
    for (uint32_t i = 0; i < nStas; ++i)
    {
        const uint16_t port = 7000 + i;
        Address sinkAddr(InetSocketAddress(Ipv4Address::GetAny(), port));
        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", sinkAddr);
        auto sink = sinkHelper.Install(apNode.Get(0));
        sink.Start(Seconds(0.0));
        sink.Stop(Seconds(simTime));
        sinkApps.Add(sink);

        BulkSendHelper sourceHelper("ns3::TcpSocketFactory", InetSocketAddress(apAddr, port));
        sourceHelper.SetAttribute("MaxBytes", UintegerValue(0));
        sourceHelper.SetAttribute("Tos", UintegerValue(0xb8));
        auto src = sourceHelper.Install(staNodes.Get(i));
        src.Start(Seconds(appStart));
        src.Stop(Seconds(simTime));
        sourceApps.Add(src);
    }

    FlowMonitorHelper flowMonHelper;
    Ptr<FlowMonitor> flowMonitor = flowMonHelper.InstallAll();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowMonHelper.GetClassifier());
    NS_ABORT_MSG_IF(!classifier, "Expected Ipv4FlowClassifier from FlowMonitorHelper");

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

    flowMonitor->CheckForLostPackets();

    std::map<double, uint64_t> delayBinsMs;
    std::map<double, uint64_t> jitterBinsMs;
    uint64_t totalDelaySamples = 0;
    uint64_t totalJitterSamples = 0;
    uint64_t delayAbove10Ms = 0;
    uint64_t delayAbove20Ms = 0;
    uint64_t totalTxPackets = 0;
    uint64_t totalRxPackets = 0;
    uint64_t totalLostPackets = 0;
    std::vector<double> flowCompletionMs;
    std::vector<double> flowRetransRatio;

    for (const auto& [flowId, st] : flowMonitor->GetFlowStats())
    {
        const auto tuple = classifier->FindFlow(flowId);
        if (tuple.protocol != 6 || tuple.destinationAddress != apAddr)
        {
            continue;
        }

        totalTxPackets += st.txPackets;
        totalRxPackets += st.rxPackets;
        totalLostPackets += st.lostPackets;
        if (st.txPackets > 0)
        {
            const auto retrans = static_cast<double>(st.txPackets - st.rxPackets) /
                                 static_cast<double>(st.txPackets);
            flowRetransRatio.push_back(retrans);
        }
        if (st.rxPackets > 0)
        {
            flowCompletionMs.push_back((st.timeLastRxPacket - st.timeFirstTxPacket).GetSeconds() *
                                       1000.0);
        }

        for (uint32_t i = 0; i < st.delayHistogram.GetNBins(); ++i)
        {
            const auto count = st.delayHistogram.GetBinCount(i);
            if (count == 0)
            {
                continue;
            }
            const auto binEndMs = st.delayHistogram.GetBinEnd(i) * 1000.0;
            delayBinsMs[binEndMs] += count;
            totalDelaySamples += count;
            if (binEndMs > 10.0)
            {
                delayAbove10Ms += count;
            }
            if (binEndMs > 20.0)
            {
                delayAbove20Ms += count;
            }
        }

        for (uint32_t i = 0; i < st.jitterHistogram.GetNBins(); ++i)
        {
            const auto count = st.jitterHistogram.GetBinCount(i);
            if (count == 0)
            {
                continue;
            }
            const auto binEndMs = st.jitterHistogram.GetBinEnd(i) * 1000.0;
            jitterBinsMs[binEndMs] += count;
            totalJitterSamples += count;
        }
    }

    const auto delayPct = GetHistogramPercentiles(delayBinsMs, totalDelaySamples);
    const auto jitterPct = GetHistogramPercentiles(jitterBinsMs, totalJitterSamples);
    const auto completionPct = GetPercentiles(flowCompletionMs);
    const auto retransPct = GetPercentiles(flowRetransRatio);
    const auto delayCdf = BuildCdf(delayBinsMs, totalDelaySamples);
    const auto jitterCdf = BuildCdf(jitterBinsMs, totalJitterSamples);
    const auto completionCdf = [&]() {
        std::vector<std::pair<double, double>> cdf;
        if (flowCompletionMs.empty())
        {
            return cdf;
        }
        std::vector<double> sorted = flowCompletionMs;
        std::sort(sorted.begin(), sorted.end());
        cdf.reserve(sorted.size());
        for (std::size_t i = 0; i < sorted.size(); ++i)
        {
            cdf.emplace_back(sorted[i], static_cast<double>(i + 1) / static_cast<double>(sorted.size()));
        }
        return cdf;
    }();

    WriteCdfCsv(tailPrefix + "-delay-cdf.csv", delayCdf);
    WriteCdfCsv(tailPrefix + "-jitter-cdf.csv", jitterCdf);
    WriteCdfCsv(tailPrefix + "-completion-cdf.csv", completionCdf);

    std::ofstream summary(tailPrefix + "-summary.csv");
    summary << "metric,value\n";
    summary << std::fixed << std::setprecision(6);
    summary << "flows_considered," << flowCompletionMs.size() << "\n";
    summary << "tx_packets," << totalTxPackets << "\n";
    summary << "rx_packets," << totalRxPackets << "\n";
    summary << "lost_packets," << totalLostPackets << "\n";
    summary << "delay_samples," << totalDelaySamples << "\n";
    summary << "delay_p50_ms," << delayPct.p50 << "\n";
    summary << "delay_p90_ms," << delayPct.p90 << "\n";
    summary << "delay_p95_ms," << delayPct.p95 << "\n";
    summary << "delay_p99_ms," << delayPct.p99 << "\n";
    summary << "delay_p99_9_ms," << delayPct.p999 << "\n";
    summary << "delay_over_10ms_packets," << delayAbove10Ms << "\n";
    summary << "delay_over_20ms_packets," << delayAbove20Ms << "\n";
    summary << "jitter_samples," << totalJitterSamples << "\n";
    summary << "jitter_p50_ms," << jitterPct.p50 << "\n";
    summary << "jitter_p90_ms," << jitterPct.p90 << "\n";
    summary << "jitter_p95_ms," << jitterPct.p95 << "\n";
    summary << "jitter_p99_ms," << jitterPct.p99 << "\n";
    summary << "jitter_p99_9_ms," << jitterPct.p999 << "\n";
    summary << "flow_completion_p50_ms," << completionPct.p50 << "\n";
    summary << "flow_completion_p90_ms," << completionPct.p90 << "\n";
    summary << "flow_completion_p95_ms," << completionPct.p95 << "\n";
    summary << "flow_completion_p99_ms," << completionPct.p99 << "\n";
    summary << "flow_completion_p99_9_ms," << completionPct.p999 << "\n";
    summary << "retrans_ratio_p50," << retransPct.p50 << "\n";
    summary << "retrans_ratio_p90," << retransPct.p90 << "\n";
    summary << "retrans_ratio_p95," << retransPct.p95 << "\n";
    summary << "retrans_ratio_p99," << retransPct.p99 << "\n";
    summary << "retrans_ratio_p99_9," << retransPct.p999 << "\n";

    std::ofstream plt(tailPrefix + "-cdf.plt");
    plt << "set terminal pngcairo size 1280,720\n";
    plt << "set datafile separator ','\n";
    plt << "set output '" << tailPrefix << "-cdf.png'\n";
    plt << "set title 'UHR MLD Tail-Latency CDFs'\n";
    plt << "set xlabel 'Time (ms)'\n";
    plt << "set ylabel 'CDF'\n";
    plt << "set key right bottom\n";
    plt << "set grid\n";
    plt << "set xrange [0:*]\n";
    plt << "set yrange [0:1]\n";
    plt << "plot '" << tailPrefix << "-delay-cdf.csv' using 1:2 with lines lw 2 title 'Delay CDF',\\\n";
    plt << "     '" << tailPrefix
        << "-jitter-cdf.csv' using 1:2 with lines lw 2 title 'Jitter CDF',\\\n";
    plt << "     '" << tailPrefix
        << "-completion-cdf.csv' using 1:2 with lines lw 2 title 'Flow completion CDF'\n";

    std::cout << "UHR MLD AP + " << nStas << " MLD STAs TCP VO uplink summary\n";
    std::cout << "  Links per MLD: " << nLinks << "\n";
    std::cout << "  AP IP: " << apAddr << "\n";
    std::cout << "  Data mode: " << dataMode << ", channel width: " << channelWidth << " MHz\n";
    std::cout << "  RTS/CTS: forced (threshold=0)\n";
    std::cout << "  VO mapping: TOS=0xb8 for all sources\n";
    std::cout << "  Total RX bytes at AP: " << totalRx << "\n";
    std::cout << "  Aggregate throughput: " << throughputMbps << " Mbit/s\n";
    std::cout << "  Delay p95/p99/p99.9 (ms): " << delayPct.p95 << " / " << delayPct.p99 << " / "
              << delayPct.p999 << "\n";
    std::cout << "  Jitter p95/p99/p99.9 (ms): " << jitterPct.p95 << " / " << jitterPct.p99
              << " / " << jitterPct.p999 << "\n";
    std::cout << "  Flow completion p95/p99/p99.9 (ms): " << completionPct.p95 << " / "
              << completionPct.p99 << " / " << completionPct.p999 << "\n";
    std::cout << "  Tail outputs: " << tailPrefix << "-summary.csv, " << tailPrefix
              << "-delay-cdf.csv, " << tailPrefix << "-jitter-cdf.csv, " << tailPrefix
              << "-completion-cdf.csv, " << tailPrefix << "-cdf.plt\n";

    Simulator::Destroy();
    return 0;
}
