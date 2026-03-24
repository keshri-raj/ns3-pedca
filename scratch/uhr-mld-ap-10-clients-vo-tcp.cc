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
#include <unordered_map>
#include <unordered_set>
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

struct VoStaStats
{
    std::unordered_map<uint64_t, double> enqueueTimeByUid;
    std::map<uint8_t, double> lastBackoffStartByLink;
    uint32_t queueDepthLast{0};
    double queueDepthLastUpdateSec{0.0};
    double queueDepthArea{0.0};
    uint32_t queueDepthMax{0};
    uint64_t queueDrops{0};
    uint64_t queueDropBeforeEnqueue{0};
    uint64_t queueExpiredDrops{0};
};

struct VoMacStats
{
    std::vector<VoStaStats> sta;
    std::vector<double> queueDelayMs;
    std::vector<double> accessDelayMs;
    uint64_t macTxDataFailed{0};
    uint64_t macTxFinalDataFailed{0};
    uint64_t macTxRtsFailed{0};
};

struct AccessStats
{
    std::vector<std::map<uint8_t, double>> lastBackoffStartByLink;
    std::vector<double> accessDelayMs;
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

void
OnVoQueueEnqueue(VoMacStats* stats, uint32_t staIdx, Ptr<const WifiMpdu> mpdu)
{
    if (!mpdu || !mpdu->GetPacket())
    {
        return;
    }
    stats->sta[staIdx].enqueueTimeByUid[mpdu->GetPacket()->GetUid()] = Simulator::Now().GetSeconds();
}

void
OnVoQueueDequeue(VoMacStats* stats, uint32_t staIdx, Ptr<const WifiMpdu> mpdu)
{
    if (!mpdu || !mpdu->GetPacket())
    {
        return;
    }
    auto& st = stats->sta[staIdx];
    const auto uid = mpdu->GetPacket()->GetUid();
    if (auto it = st.enqueueTimeByUid.find(uid); it != st.enqueueTimeByUid.end())
    {
        stats->queueDelayMs.push_back((Simulator::Now().GetSeconds() - it->second) * 1000.0);
        st.enqueueTimeByUid.erase(it);
    }
}

void
OnVoQueueDrop(VoMacStats* stats, uint32_t staIdx, Ptr<const WifiMpdu> mpdu)
{
    stats->sta[staIdx].queueDrops++;
    if (mpdu && mpdu->GetPacket())
    {
        stats->sta[staIdx].enqueueTimeByUid.erase(mpdu->GetPacket()->GetUid());
    }
}

void
OnVoQueueDropBeforeEnqueue(VoMacStats* stats, uint32_t staIdx, Ptr<const WifiMpdu> mpdu)
{
    stats->sta[staIdx].queueDropBeforeEnqueue++;
    if (mpdu && mpdu->GetPacket())
    {
        stats->sta[staIdx].enqueueTimeByUid.erase(mpdu->GetPacket()->GetUid());
    }
}

void
OnVoQueueExpired(VoMacStats* stats, uint32_t staIdx, Ptr<const WifiMpdu> mpdu)
{
    stats->sta[staIdx].queueExpiredDrops++;
    if (mpdu && mpdu->GetPacket())
    {
        stats->sta[staIdx].enqueueTimeByUid.erase(mpdu->GetPacket()->GetUid());
    }
}

void
OnVoQueueDepthChange(VoMacStats* stats, uint32_t staIdx, uint32_t oldVal, uint32_t newVal)
{
    auto& st = stats->sta[staIdx];
    const auto nowSec = Simulator::Now().GetSeconds();
    st.queueDepthArea += oldVal * std::max(0.0, nowSec - st.queueDepthLastUpdateSec);
    st.queueDepthLast = newVal;
    st.queueDepthLastUpdateSec = nowSec;
    st.queueDepthMax = std::max(st.queueDepthMax, newVal);
}

void
OnVoBackoff(VoMacStats* stats, uint32_t staIdx, uint32_t slots, uint8_t linkId)
{
    (void)slots;
    stats->sta[staIdx].lastBackoffStartByLink[linkId] = Simulator::Now().GetSeconds();
}

void
OnVoTxop(VoMacStats* stats, uint32_t staIdx, Time start, Time duration, uint8_t linkId)
{
    (void)duration;
    auto& st = stats->sta[staIdx];
    if (auto it = st.lastBackoffStartByLink.find(linkId); it != st.lastBackoffStartByLink.end())
    {
        stats->accessDelayMs.push_back((start.GetSeconds() - it->second) * 1000.0);
    }
}

void
OnAccessBackoff(AccessStats* stats, uint32_t idx, uint32_t slots, uint8_t linkId)
{
    (void)slots;
    stats->lastBackoffStartByLink[idx][linkId] = Simulator::Now().GetSeconds();
}

void
OnAccessTxop(AccessStats* stats, uint32_t idx, Time start, Time duration, uint8_t linkId)
{
    (void)duration;
    if (auto it = stats->lastBackoffStartByLink[idx].find(linkId);
        it != stats->lastBackoffStartByLink[idx].end())
    {
        stats->accessDelayMs.push_back((start.GetSeconds() - it->second) * 1000.0);
    }
}

void
OnMacTxDataFailed(VoMacStats* stats, Mac48Address)
{
    stats->macTxDataFailed++;
}

void
OnMacTxFinalDataFailed(VoMacStats* stats, Mac48Address)
{
    stats->macTxFinalDataFailed++;
}

void
OnMacTxRtsFailed(VoMacStats* stats, Mac48Address)
{
    stats->macTxRtsFailed++;
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
    bool splitStasAcrossLinks = false;
    bool enablePedca = true;
    bool distributePedcaAcrossLinks = false;
    bool enableUl = true;
    bool enableDl = true;
    std::string pcapPrefix = "scratch/uhr-mld-ap-10-clients-vo-tcp";
    std::string animFile = "scratch/uhr-mld-ap-10-clients-vo-tcp.xml";
    std::string tailPrefix = "scratch/uhr-mld-ap-10-clients-vo-tcp-tail";
    std::string accessPrefix = "scratch/uhr-mld-ap-10-clients-vo-tcp-access";

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
    cmd.AddValue("splitStasAcrossLinks",
                 "For nLinks=2, map half STAs to link 0 and half to link 1 using TID-to-link mapping",
                 splitStasAcrossLinks);
    cmd.AddValue("enablePedca", "Enable UHR VO P-EDCA behavior", enablePedca);
    cmd.AddValue("distributePedcaAcrossLinks",
                 "For multi-link STAs, allow P-EDCA only on a designated STA link to distribute contention",
                 distributePedcaAcrossLinks);
    cmd.AddValue("enableUl", "Enable UL TCP flows (STA -> AP)", enableUl);
    cmd.AddValue("enableDl", "Enable DL TCP flows (AP -> STA)", enableDl);
    cmd.AddValue("pcapPrefix", "PCAP prefix/path", pcapPrefix);
    cmd.AddValue("animFile", "NetAnim XML file path", animFile);
    cmd.AddValue("tailPrefix", "Prefix for tail-latency CSV/plot outputs", tailPrefix);
    cmd.AddValue("accessPrefix", "Prefix for channel access CDF outputs", accessPrefix);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(nLinks < 1 || nLinks > 2, "nLinks must be 1 or 2");
    NS_ABORT_MSG_IF(splitStasAcrossLinks && nLinks != 2,
                    "splitStasAcrossLinks requires nLinks=2");

    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(payloadSize));
    Config::SetDefault("ns3::QosTxop::EnableUhrPedca", BooleanValue(enablePedca));
    Config::SetDefault("ns3::QosTxop::DistributePedcaAcrossLinks",
                       BooleanValue(distributePedcaAcrossLinks));

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

    if (splitStasAcrossLinks)
    {
        const std::string allTidsLink0 = "0,1,2,3,4,5,6,7 0";
        const std::string allTidsLink1 = "0,1,2,3,4,5,6,7 1";
        for (uint32_t i = 0; i < nStas; ++i)
        {
            auto staNetDev = DynamicCast<WifiNetDevice>(staDevices.Get(i));
            NS_ABORT_MSG_IF(!staNetDev, "Expected WifiNetDevice for STA");
            auto staMac = staNetDev->GetMac();
            NS_ABORT_MSG_IF(!staMac, "Expected WifiMac on STA");
            auto eht = staMac->GetEhtConfiguration();
            NS_ABORT_MSG_IF(!eht, "Expected EHT configuration on STA");
            const auto& mapping = (i < nStas / 2) ? allTidsLink0 : allTidsLink1;
            eht->SetAttribute("TidToLinkMappingNegSupport",
                              EnumValue(WifiTidToLinkMappingNegSupport::ANY_LINK_SET));
            eht->SetAttribute("TidToLinkMappingUl", StringValue(mapping));
            eht->SetAttribute("TidToLinkMappingDl", StringValue(mapping));
        }
    }

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
    std::unordered_set<Ipv4Address> staAddrs;
    for (uint32_t i = 0; i < nStas; ++i)
    {
        staAddrs.insert(ifaces.GetAddress(i + 1));
    }

    if (useStaticSetup)
    {
        NeighborCacheHelper nbCache;
        nbCache.PopulateNeighborCache();
    }

    ApplicationContainer sinkApps;
    ApplicationContainer sourceApps;
    ApplicationContainer dlSinkApps;
    ApplicationContainer dlSourceApps;
    for (uint32_t i = 0; i < nStas; ++i)
    {
        const uint16_t port = 7000 + i;
        if (enableUl)
        {
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

        if (enableDl)
        {
            const uint16_t dlPort = 8000 + i;
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
    }

    VoMacStats voMacStats;
    voMacStats.sta.resize(nStas);
    for (uint32_t i = 0; i < nStas; ++i)
    {
        auto staNetDev = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        NS_ABORT_MSG_IF(!staNetDev, "Expected WifiNetDevice for STA");
        auto staMac = staNetDev->GetMac();
        NS_ABORT_MSG_IF(!staMac, "Expected WifiMac on STA");
        auto voTxop = staMac->GetQosTxop(AC_VO);
        NS_ABORT_MSG_IF(!voTxop, "Expected VO QosTxop");
        auto voQueue = voTxop->GetWifiMacQueue();
        NS_ABORT_MSG_IF(!voQueue, "Expected VO WifiMacQueue");

        voQueue->TraceConnectWithoutContext("Enqueue",
                                            MakeBoundCallback(&OnVoQueueEnqueue, &voMacStats, i));
        voQueue->TraceConnectWithoutContext("Dequeue",
                                            MakeBoundCallback(&OnVoQueueDequeue, &voMacStats, i));
        voQueue->TraceConnectWithoutContext("Drop",
                                            MakeBoundCallback(&OnVoQueueDrop, &voMacStats, i));
        voQueue->TraceConnectWithoutContext("DropBeforeEnqueue",
                                            MakeBoundCallback(&OnVoQueueDropBeforeEnqueue,
                                                              &voMacStats,
                                                              i));
        voQueue->TraceConnectWithoutContext("Expired",
                                            MakeBoundCallback(&OnVoQueueExpired, &voMacStats, i));
        voQueue->TraceConnectWithoutContext("PacketsInQueue",
                                            MakeBoundCallback(&OnVoQueueDepthChange,
                                                              &voMacStats,
                                                              i));
        voTxop->TraceConnectWithoutContext("BackoffTrace",
                                           MakeBoundCallback(&OnVoBackoff, &voMacStats, i));
        voTxop->TraceConnectWithoutContext("TxopTrace",
                                           MakeBoundCallback(&OnVoTxop, &voMacStats, i));
        for (uint8_t linkId = 0; linkId < nLinks; ++linkId)
        {
            auto rsm = staMac->GetWifiRemoteStationManager(linkId);
            NS_ABORT_MSG_IF(!rsm, "Expected WifiRemoteStationManager");
            rsm->TraceConnectWithoutContext("MacTxDataFailed",
                                            MakeBoundCallback(&OnMacTxDataFailed, &voMacStats));
            rsm->TraceConnectWithoutContext("MacTxFinalDataFailed",
                                            MakeBoundCallback(&OnMacTxFinalDataFailed, &voMacStats));
            rsm->TraceConnectWithoutContext("MacTxRtsFailed",
                                            MakeBoundCallback(&OnMacTxRtsFailed, &voMacStats));
        }
    }

    AccessStats ulAccessStats;
    AccessStats dlAccessStats;
    ulAccessStats.lastBackoffStartByLink.resize(nStas);
    dlAccessStats.lastBackoffStartByLink.resize(1);
    for (uint32_t i = 0; i < nStas; ++i)
    {
        auto staNetDev = DynamicCast<WifiNetDevice>(staDevices.Get(i));
        NS_ABORT_MSG_IF(!staNetDev, "Expected WifiNetDevice for STA");
        auto staMac = staNetDev->GetMac();
        auto voTxop = staMac->GetQosTxop(AC_VO);
        if (enableUl)
        {
            voTxop->TraceConnectWithoutContext("BackoffTrace",
                                               MakeBoundCallback(&OnAccessBackoff,
                                                                 &ulAccessStats,
                                                                 i));
            voTxop->TraceConnectWithoutContext("TxopTrace",
                                               MakeBoundCallback(&OnAccessTxop,
                                                                 &ulAccessStats,
                                                                 i));
        }
    }
    if (enableDl)
    {
        auto apNetDev = DynamicCast<WifiNetDevice>(apDevice.Get(0));
        NS_ABORT_MSG_IF(!apNetDev, "Expected WifiNetDevice for AP");
        auto apMac = apNetDev->GetMac();
        auto apVoTxop = apMac->GetQosTxop(AC_VO);
        apVoTxop->TraceConnectWithoutContext("BackoffTrace",
                                             MakeBoundCallback(&OnAccessBackoff,
                                                               &dlAccessStats,
                                                               0));
        apVoTxop->TraceConnectWithoutContext("TxopTrace",
                                             MakeBoundCallback(&OnAccessTxop,
                                                               &dlAccessStats,
                                                               0));
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
    uint64_t totalDlRx = 0;
    for (uint32_t i = 0; i < sinkApps.GetN(); ++i)
    {
        auto sink = DynamicCast<PacketSink>(sinkApps.Get(i));
        const uint64_t rx = sink->GetTotalRx();
        totalRx += rx;
        std::cout << "STA-" << i << " -> AP bytes: " << rx << "\n";
    }
    for (uint32_t i = 0; i < dlSinkApps.GetN(); ++i)
    {
        auto sink = DynamicCast<PacketSink>(dlSinkApps.Get(i));
        totalDlRx += sink->GetTotalRx();
    }

    const double active = std::max(0.001, simTime - appStart);
    const double throughputMbps = (totalRx * 8.0) / (active * 1e6);
    const double dlThroughputMbps = (totalDlRx * 8.0) / (active * 1e6);

    flowMonitor->CheckForLostPackets();

    std::map<double, uint64_t> delayBinsMs;
    std::map<double, uint64_t> jitterBinsMs;
    std::map<double, uint64_t> ulDelayBinsMs;
    std::map<double, uint64_t> ulJitterBinsMs;
    std::map<double, uint64_t> dlDelayBinsMs;
    std::map<double, uint64_t> dlJitterBinsMs;
    uint64_t totalDelaySamples = 0;
    uint64_t totalJitterSamples = 0;
    uint64_t ulDelaySamples = 0;
    uint64_t ulJitterSamples = 0;
    uint64_t dlDelaySamples = 0;
    uint64_t dlJitterSamples = 0;
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
        if (tuple.protocol != 6)
        {
            continue;
        }
        const bool isUl = (tuple.destinationAddress == apAddr);
        const bool isDl = staAddrs.find(tuple.destinationAddress) != staAddrs.end();
        if ((!enableUl && isUl) || (!enableDl && isDl) || (!isUl && !isDl))
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
            if (isUl)
            {
                ulDelayBinsMs[binEndMs] += count;
                ulDelaySamples += count;
            }
            if (isDl)
            {
                dlDelayBinsMs[binEndMs] += count;
                dlDelaySamples += count;
            }
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
            if (isUl)
            {
                ulJitterBinsMs[binEndMs] += count;
                ulJitterSamples += count;
            }
            if (isDl)
            {
                dlJitterBinsMs[binEndMs] += count;
                dlJitterSamples += count;
            }
        }
    }

    const auto delayPct = GetHistogramPercentiles(delayBinsMs, totalDelaySamples);
    const auto jitterPct = GetHistogramPercentiles(jitterBinsMs, totalJitterSamples);
    const auto ulDelayPct = GetHistogramPercentiles(ulDelayBinsMs, ulDelaySamples);
    const auto ulJitterPct = GetHistogramPercentiles(ulJitterBinsMs, ulJitterSamples);
    const auto dlDelayPct = GetHistogramPercentiles(dlDelayBinsMs, dlDelaySamples);
    const auto dlJitterPct = GetHistogramPercentiles(dlJitterBinsMs, dlJitterSamples);
    const auto completionPct = GetPercentiles(flowCompletionMs);
    const auto retransPct = GetPercentiles(flowRetransRatio);
    const auto queueDelayPct = GetPercentiles(voMacStats.queueDelayMs);
    const auto accessDelayPct = GetPercentiles(voMacStats.accessDelayMs);
    uint64_t queueDrops = 0;
    uint64_t queueDropBeforeEnqueue = 0;
    uint64_t queueExpiredDrops = 0;
    uint32_t queueDepthMax = 0;
    double meanQueueDepth = 0.0;
    for (auto& st : voMacStats.sta)
    {
        st.queueDepthArea += st.queueDepthLast * std::max(0.0, simTime - st.queueDepthLastUpdateSec);
        meanQueueDepth += st.queueDepthArea / std::max(0.001, simTime);
        queueDepthMax = std::max(queueDepthMax, st.queueDepthMax);
        queueDrops += st.queueDrops;
        queueDropBeforeEnqueue += st.queueDropBeforeEnqueue;
        queueExpiredDrops += st.queueExpiredDrops;
    }
    meanQueueDepth /= std::max<uint32_t>(1, nStas);
    const double macDataFailRate =
        (totalTxPackets > 0) ? static_cast<double>(voMacStats.macTxDataFailed) / totalTxPackets : 0.0;
    const double macFinalDataFailRate =
        (totalTxPackets > 0) ? static_cast<double>(voMacStats.macTxFinalDataFailed) / totalTxPackets
                             : 0.0;
    const double macRtsFailRate =
        (totalTxPackets > 0) ? static_cast<double>(voMacStats.macTxRtsFailed) / totalTxPackets : 0.0;
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
    summary << "pedca_enabled," << (enablePedca ? 1 : 0) << "\n";
    summary << "split_stas_across_links," << (splitStasAcrossLinks ? 1 : 0) << "\n";
    summary << "distribute_pedca_across_links," << (distributePedcaAcrossLinks ? 1 : 0) << "\n";
    summary << "enable_ul," << (enableUl ? 1 : 0) << "\n";
    summary << "enable_dl," << (enableDl ? 1 : 0) << "\n";
    summary << "stas_link0_target," << (splitStasAcrossLinks ? (nStas / 2) : nStas) << "\n";
    summary << "stas_link1_target," << (splitStasAcrossLinks ? (nStas - nStas / 2) : 0) << "\n";
    summary << "flows_considered," << flowCompletionMs.size() << "\n";
    summary << "aggregate_throughput_mbps," << throughputMbps << "\n";
    summary << "dl_throughput_mbps," << dlThroughputMbps << "\n";
    summary << "delay_samples_present," << (totalDelaySamples > 0 ? 1 : 0) << "\n";
    summary << "jitter_samples_present," << (totalJitterSamples > 0 ? 1 : 0) << "\n";
    summary << "completion_samples_present," << (flowCompletionMs.empty() ? 0 : 1) << "\n";
    summary << "tx_packets," << totalTxPackets << "\n";
    summary << "rx_packets," << totalRxPackets << "\n";
    summary << "lost_packets," << totalLostPackets << "\n";
    summary << "delay_samples," << totalDelaySamples << "\n";
    summary << "delay_p50_ms," << delayPct.p50 << "\n";
    summary << "delay_p90_ms," << delayPct.p90 << "\n";
    summary << "delay_p95_ms," << delayPct.p95 << "\n";
    summary << "delay_p99_ms," << delayPct.p99 << "\n";
    summary << "delay_p99_9_ms," << delayPct.p999 << "\n";
    summary << "ul_delay_p50_ms," << ulDelayPct.p50 << "\n";
    summary << "ul_delay_p90_ms," << ulDelayPct.p90 << "\n";
    summary << "ul_delay_p95_ms," << ulDelayPct.p95 << "\n";
    summary << "ul_delay_p99_ms," << ulDelayPct.p99 << "\n";
    summary << "ul_delay_p99_9_ms," << ulDelayPct.p999 << "\n";
    summary << "dl_delay_p50_ms," << dlDelayPct.p50 << "\n";
    summary << "dl_delay_p90_ms," << dlDelayPct.p90 << "\n";
    summary << "dl_delay_p95_ms," << dlDelayPct.p95 << "\n";
    summary << "dl_delay_p99_ms," << dlDelayPct.p99 << "\n";
    summary << "dl_delay_p99_9_ms," << dlDelayPct.p999 << "\n";
    summary << "delay_over_10ms_packets," << delayAbove10Ms << "\n";
    summary << "delay_over_20ms_packets," << delayAbove20Ms << "\n";
    summary << "jitter_samples," << totalJitterSamples << "\n";
    summary << "jitter_p50_ms," << jitterPct.p50 << "\n";
    summary << "jitter_p90_ms," << jitterPct.p90 << "\n";
    summary << "jitter_p95_ms," << jitterPct.p95 << "\n";
    summary << "jitter_p99_ms," << jitterPct.p99 << "\n";
    summary << "jitter_p99_9_ms," << jitterPct.p999 << "\n";
    summary << "ul_jitter_p50_ms," << ulJitterPct.p50 << "\n";
    summary << "ul_jitter_p90_ms," << ulJitterPct.p90 << "\n";
    summary << "ul_jitter_p95_ms," << ulJitterPct.p95 << "\n";
    summary << "ul_jitter_p99_ms," << ulJitterPct.p99 << "\n";
    summary << "ul_jitter_p99_9_ms," << ulJitterPct.p999 << "\n";
    summary << "dl_jitter_p50_ms," << dlJitterPct.p50 << "\n";
    summary << "dl_jitter_p90_ms," << dlJitterPct.p90 << "\n";
    summary << "dl_jitter_p95_ms," << dlJitterPct.p95 << "\n";
    summary << "dl_jitter_p99_ms," << dlJitterPct.p99 << "\n";
    summary << "dl_jitter_p99_9_ms," << dlJitterPct.p999 << "\n";
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
    summary << "vo_mac_tx_data_failed," << voMacStats.macTxDataFailed << "\n";
    summary << "vo_mac_tx_final_data_failed," << voMacStats.macTxFinalDataFailed << "\n";
    summary << "vo_mac_tx_rts_failed," << voMacStats.macTxRtsFailed << "\n";
    summary << "vo_mac_tx_data_failed_rate," << macDataFailRate << "\n";
    summary << "vo_mac_tx_final_data_failed_rate," << macFinalDataFailRate << "\n";
    summary << "vo_mac_tx_rts_failed_rate," << macRtsFailRate << "\n";
    summary << "vo_channel_access_delay_p50_ms," << accessDelayPct.p50 << "\n";
    summary << "vo_channel_access_delay_p90_ms," << accessDelayPct.p90 << "\n";
    summary << "vo_channel_access_delay_p95_ms," << accessDelayPct.p95 << "\n";
    summary << "vo_channel_access_delay_p99_ms," << accessDelayPct.p99 << "\n";
    summary << "vo_channel_access_delay_p99_9_ms," << accessDelayPct.p999 << "\n";
    summary << "vo_queue_delay_p50_ms," << queueDelayPct.p50 << "\n";
    summary << "vo_queue_delay_p90_ms," << queueDelayPct.p90 << "\n";
    summary << "vo_queue_delay_p95_ms," << queueDelayPct.p95 << "\n";
    summary << "vo_queue_delay_p99_ms," << queueDelayPct.p99 << "\n";
    summary << "vo_queue_delay_p99_9_ms," << queueDelayPct.p999 << "\n";
    summary << "vo_queue_depth_mean_packets," << meanQueueDepth << "\n";
    summary << "vo_queue_depth_max_packets," << queueDepthMax << "\n";
    summary << "vo_queue_drop_packets," << queueDrops << "\n";
    summary << "vo_queue_drop_before_enqueue_packets," << queueDropBeforeEnqueue << "\n";
    summary << "vo_queue_expired_drop_packets," << queueExpiredDrops << "\n";

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

    const auto ulAccessCdf = BuildCdfFromValues(ulAccessStats.accessDelayMs);
    const auto dlAccessCdf = BuildCdfFromValues(dlAccessStats.accessDelayMs);
    WriteCdfCsv(accessPrefix + "-ul-cdf.csv", ulAccessCdf);
    WriteCdfCsv(accessPrefix + "-dl-cdf.csv", dlAccessCdf);
    std::ofstream accessPlt(accessPrefix + "-cdf.plt");
    accessPlt << "set terminal pngcairo size 1200,800\n";
    accessPlt << "set datafile separator ','\n";
    accessPlt << "set output '" << accessPrefix << "-cdf.png'\n";
    accessPlt << "set title 'Channel Access Time CDF (UL vs DL)'\n";
    accessPlt << "set xlabel 'Channel Access Time (ms)'\n";
    accessPlt << "set ylabel 'CDF'\n";
    accessPlt << "set key left bottom\n";
    accessPlt << "set grid\n";
    accessPlt << "plot '" << accessPrefix
              << "-ul-cdf.csv' using 1:2 with lines lw 2 title 'UL',\\\n";
    accessPlt << "     '" << accessPrefix
              << "-dl-cdf.csv' using 1:2 with lines lw 2 title 'DL'\n";
    if (enableUl && ulAccessStats.accessDelayMs.empty())
    {
        std::cout << "  WARNING: No UL channel access samples collected\n";
    }
    if (enableDl && dlAccessStats.accessDelayMs.empty())
    {
        std::cout << "  WARNING: No DL channel access samples collected\n";
    }

    std::cout << "UHR MLD AP + " << nStas << " MLD STAs TCP VO uplink summary\n";
    std::cout << "  Links per MLD: " << nLinks << "\n";
    std::cout << "  AP IP: " << apAddr << "\n";
    std::cout << "  Data mode: " << dataMode << ", channel width: " << channelWidth << " MHz\n";
    std::cout << "  RTS/CTS: forced (threshold=0)\n";
    std::cout << "  P-EDCA enabled: " << (enablePedca ? "yes" : "no") << "\n";
    std::cout << "  P-EDCA distribution across links: "
              << (distributePedcaAcrossLinks ? "yes" : "no") << "\n";
    if (splitStasAcrossLinks)
    {
        std::cout << "  STA link split target: link0=" << (nStas / 2)
                  << ", link1=" << (nStas - nStas / 2) << "\n";
    }
    std::cout << "  VO mapping: TOS=0xb8 for all sources\n";
    std::cout << "  Total RX bytes at AP: " << totalRx << "\n";
    std::cout << "  Aggregate throughput: " << throughputMbps << " Mbit/s\n";
    std::cout << "  Delay p95/p99/p99.9 (ms): " << delayPct.p95 << " / " << delayPct.p99 << " / "
              << delayPct.p999 << "\n";
    std::cout << "  Jitter p95/p99/p99.9 (ms): " << jitterPct.p95 << " / " << jitterPct.p99
              << " / " << jitterPct.p999 << "\n";
    std::cout << "  Flow completion p95/p99/p99.9 (ms): " << completionPct.p95 << " / "
              << completionPct.p99 << " / " << completionPct.p999 << "\n";
    std::cout << "  VO MAC data failed/final failed: " << voMacStats.macTxDataFailed << " / "
              << voMacStats.macTxFinalDataFailed << "\n";
    std::cout << "  VO RTS failed (collision proxy): " << voMacStats.macTxRtsFailed << "\n";
    std::cout << "  VO channel access delay p95/p99 (ms): " << accessDelayPct.p95 << " / "
              << accessDelayPct.p99 << "\n";
    std::cout << "  VO queue delay p95/p99 (ms): " << queueDelayPct.p95 << " / "
              << queueDelayPct.p99 << "\n";
    std::cout << "  VO queue depth mean/max (pkts): " << meanQueueDepth << " / " << queueDepthMax
              << "\n";
    std::cout << "  VO queue drops (drop/before-enqueue/expired): " << queueDrops << " / "
              << queueDropBeforeEnqueue << " / " << queueExpiredDrops << "\n";
    if (totalDelaySamples == 0)
    {
        std::cout << "  WARNING: No delay samples collected; delay CDF CSV contains only header\n";
    }
    if (totalJitterSamples == 0)
    {
        std::cout << "  WARNING: No jitter samples collected; jitter CDF CSV contains only header\n";
    }
    if (flowCompletionMs.empty())
    {
        std::cout
            << "  WARNING: No flow completion samples collected; completion CDF CSV contains only header\n";
    }
    std::cout << "  Tail outputs: " << tailPrefix << "-summary.csv, " << tailPrefix
              << "-delay-cdf.csv, " << tailPrefix << "-jitter-cdf.csv, " << tailPrefix
              << "-completion-cdf.csv, " << tailPrefix << "-cdf.plt\n";

    Simulator::Destroy();
    return 0;
}
