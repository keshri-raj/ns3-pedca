#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EthUdpBasic");

int
main(int argc, char* argv[])
{
    double simTime = 5.0;
    double appStart = 1.0;
    uint32_t packetSize = 1024;
    std::string dataRate = "100Mbps";
    std::string delay = "2ms";

    CommandLine cmd(__FILE__);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("appStart", "Client start time in seconds", appStart);
    cmd.AddValue("packetSize", "UDP payload size in bytes", packetSize);
    cmd.AddValue("dataRate", "PointToPoint link data rate", dataRate);
    cmd.AddValue("delay", "PointToPoint link delay", delay);
    cmd.Parse(argc, argv);

    NodeContainer nodes;
    nodes.Create(2);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue(dataRate));
    p2p.SetChannelAttribute("Delay", StringValue(delay));
    NetDeviceContainer devices = p2p.Install(nodes);

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifaces = ipv4.Assign(devices);

    const uint16_t port = 5000;
    UdpServerHelper server(port);
    ApplicationContainer serverApps = server.Install(nodes.Get(1));
    serverApps.Start(Seconds(0.5));
    serverApps.Stop(Seconds(simTime));

    UdpClientHelper client(ifaces.GetAddress(1), port);
    client.SetAttribute("MaxPackets", UintegerValue(0xFFFFFFFF));
    client.SetAttribute("Interval", TimeValue(MicroSeconds(200)));
    client.SetAttribute("PacketSize", UintegerValue(packetSize));
    ApplicationContainer clientApps = client.Install(nodes.Get(0));
    clientApps.Start(Seconds(appStart));
    clientApps.Stop(Seconds(simTime));

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    auto udpServer = DynamicCast<UdpServer>(serverApps.Get(0));
    const uint64_t rxPackets = udpServer->GetReceived();
    const uint64_t rxBytes = rxPackets * packetSize;
    const double active = std::max(0.001, simTime - appStart);
    const double throughputMbps = (rxBytes * 8.0) / (active * 1e6);

    std::cout << "ETH-UDP basic summary\n";
    std::cout << "  Sender: " << ifaces.GetAddress(0) << "\n";
    std::cout << "  Receiver: " << ifaces.GetAddress(1) << "\n";
    std::cout << "  Received packets: " << rxPackets << "\n";
    std::cout << "  Received bytes:   " << rxBytes << "\n";
    std::cout << "  Throughput:       " << throughputMbps << " Mbit/s\n";

    Simulator::Destroy();
    return 0;
}
