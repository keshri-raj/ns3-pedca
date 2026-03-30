import sys

path = "c:/ns-3/scratch/uhr-mld-mixed-pedca.cc"
with open(path, "r") as f:
    content = f.read()

# Edit 1: Attributes and prefixes
content = content.replace(
'''    bool enableDl = true;
    std::string pcapPrefix = "scratch/uhr-mld-ap-10-clients-vo-tcp";
    std::string animFile = "scratch/uhr-mld-ap-10-clients-vo-tcp.xml";
    std::string tailPrefix = "scratch/uhr-mld-ap-10-clients-vo-tcp-tail";
    std::string accessPrefix = "scratch/uhr-mld-ap-10-clients-vo-tcp-access";''',
'''    bool enableDl = true;
    uint32_t numPedcaStas = 5;
    std::string pcapPrefix = "scratch/uhr-mld-mixed-pedca";
    std::string animFile = "scratch/uhr-mld-mixed-pedca.xml";
    std::string tailPrefix = "scratch/uhr-mld-mixed-pedca-tail";
    std::string accessPrefix = "scratch/uhr-mld-mixed-pedca-access";'''
)

# Edit 2: cmd.AddValue
content = content.replace(
'''    cmd.AddValue("enableUl", "Enable UL TCP flows (STA -> AP)", enableUl);''',
'''    cmd.AddValue("enableUl", "Enable UL TCP flows (STA -> AP)", enableUl);
    cmd.AddValue("numPedcaStas", "Number of STAs configured as P-EDCA", numPedcaStas);'''
)

# Edit 3: Remove Config::SetDefault
content = content.replace(
'''    Config::SetDefault("ns3::QosTxop::EnableUhrPedca", BooleanValue(enablePedca));
    Config::SetDefault("ns3::QosTxop::DistributePedcaAcrossLinks",
                       BooleanValue(distributePedcaAcrossLinks));''',
'''    // P-EDCA settings applied selectively below'''
)

# Edit 4: Apply P-EDCA settings per-STA
content = content.replace(
'''        auto voQueue = voTxop->GetWifiMacQueue();
        NS_ABORT_MSG_IF(!voQueue, "Expected VO WifiMacQueue");''',
'''        auto voQueue = voTxop->GetWifiMacQueue();
        NS_ABORT_MSG_IF(!voQueue, "Expected VO WifiMacQueue");

        bool isPedca = (i < numPedcaStas) && enablePedca;
        voTxop->SetAttribute("EnableUhrPedca", BooleanValue(isPedca));
        voTxop->SetAttribute("DistributePedcaAcrossLinks", BooleanValue(distributePedcaAcrossLinks));'''
)

# Edit 5: Collect Throughput for P-EDCA and EDCA separately
content = content.replace(
'''    uint64_t totalRx = 0;
    uint64_t totalDlRx = 0;
    for (uint32_t i = 0; i < sinkApps.GetN(); ++i)
    {
        auto sink = DynamicCast<PacketSink>(sinkApps.Get(i));
        const uint64_t rx = sink->GetTotalRx();
        totalRx += rx;
        std::cout << "STA-" << i << " -> AP bytes: " << rx << "\\n";
    }''',
'''    uint64_t totalRx = 0;
    uint64_t totalDlRx = 0;
    uint64_t pedcaRx = 0, edcaRx = 0;
    double sumRx = 0, sumSqRx = 0;

    for (uint32_t i = 0; i < sinkApps.GetN(); ++i)
    {
        auto sink = DynamicCast<PacketSink>(sinkApps.Get(i));
        const uint64_t rx = sink->GetTotalRx();
        totalRx += rx;
        if (i < numPedcaStas) pedcaRx += rx;
        else edcaRx += rx;
        double rxd = (double)rx;
        sumRx += rxd; sumSqRx += rxd * rxd;
        std::cout << "STA-" << i << " -> AP bytes: " << rx << (i < numPedcaStas ? " (P-EDCA)" : " (EDCA)") << "\\n";
    }'''
)

# Edit 6: Output variables
content = content.replace(
'''    const double throughputMbps = (totalRx * 8.0) / (active * 1e6);
    const double dlThroughputMbps = (totalDlRx * 8.0) / (active * 1e6);''',
'''    const double throughputMbps = (totalRx * 8.0) / (active * 1e6);
    const double pedcaThroughputMbps = (pedcaRx * 8.0) / (active * 1e6);
    const double edcaThroughputMbps = (edcaRx * 8.0) / (active * 1e6);
    const double dlThroughputMbps = (totalDlRx * 8.0) / (active * 1e6);
    const double jfi = (sinkApps.GetN() > 0 && sumSqRx > 0) ? (sumRx * sumRx) / (sinkApps.GetN() * sumSqRx) : 0;'''
)

# Edit 7: Print the specific throughput and JFI to console at end
content = content.replace(
'''    std::cout << "  Total RX bytes at AP: " << totalRx << "\\n";
    std::cout << "  Aggregate throughput: " << throughputMbps << " Mbit/s\\n";''',
'''    std::cout << "  Total RX bytes at AP: " << totalRx << "\\n";
    std::cout << "  Aggregate throughput: " << throughputMbps << " Mbit/s\\n";
    std::cout << "  P-EDCA STAs throughput: " << pedcaThroughputMbps << " Mbit/s\\n";
    std::cout << "  EDCA STAs throughput: " << edcaThroughputMbps << " Mbit/s\\n";
    std::cout << "  Jains Fairness Index (JFI): " << jfi << "\\n";'''
)

# Summary CSV replacement
content = content.replace(
'''    summary << "aggregate_throughput_mbps," << throughputMbps << "\\n";''',
'''    summary << "jfi," << jfi << "\\n";
    summary << "aggregate_throughput_mbps," << throughputMbps << "\\n";
    summary << "pedca_throughput_mbps," << pedcaThroughputMbps << "\\n";
    summary << "edca_throughput_mbps," << edcaThroughputMbps << "\\n";'''
)

# Write back
with open(path, "w") as f:
    f.write(content)
print("Patch applied successfully.")
