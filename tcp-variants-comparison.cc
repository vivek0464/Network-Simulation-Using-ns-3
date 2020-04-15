/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2013 ResiliNets, ITTC, University of Kansas
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Justin P. Rohrer, Truc Anh N. Nguyen <annguyen@ittc.ku.edu>, Siddharth Gangadhar <siddharth@ittc.ku.edu>
 *
 * James P.G. Sterbenz <jpgs@ittc.ku.edu>, director
 * ResiliNets Research Group  http://wiki.ittc.ku.edu/resilinets
 * Information and Telecommunication Technology Center (ITTC)
 * and Department of Electrical Engineering and Computer Science
 * The University of Kansas Lawrence, KS USA.
 *
 * Work supported in part by NSF FIND (Future Internet Design) Program
 * under grant CNS-0626918 (Postmodern Internet Architecture),
 * NSF grant CNS-1050226 (Multilayer Network Resilience Analysis and Experimentation on GENI),
 * US Department of Defense (DoD), and ITTC at The University of Kansas.
 *
 * “TCP Westwood(+) Protocol Implementation in ns-3”
 * Siddharth Gangadhar, Trúc Anh Ngọc Nguyễn , Greeshma Umapathi, and James P.G. Sterbenz,
 * ICST SIMUTools Workshop on ns-3 (WNS3), Cannes, France, March 2013
 */

#include <iostream>
#include <fstream>
#include <string>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/error-model.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/enum.h"
#include "ns3/event-id.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/packet-sink.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("TcpVariantsComparison");

// Function passed to tracesource which invokes it each time cwnd changes.
static void 
CwndTracer(Ptr<OutputStreamWrapper>stream, uint32_t oldval, uint32_t newval)
{
	*stream->GetStream() << Simulator::Now().GetSeconds() << "," << newval << std::endl;
}

// Function to attach CwndTracer function to tracesource
static void 
TraceCwnd(std::string cwndTrFileName)
{
	AsciiTraceHelper ascii;
	Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream(cwndTrFileName.c_str());
	Config::ConnectWithoutContext("/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeBoundCallback(&CwndTracer, stream));
}

// Function to extract lost packets from flowMonitor every 0.0001 seconds and print it to file
static void 
packetDropSample(Ptr<OutputStreamWrapper> stream, Ptr<FlowMonitor> flowMonitor)
{
	std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();
	uint32_t packetDropCount = stats[1].lostPackets; // stats[1] for the FTP (tcp) flow
  	*stream->GetStream() << Simulator::Now().GetSeconds() << ","  << packetDropCount << std::endl;
	Simulator::Schedule(Seconds(0.0001), &packetDropSample, stream, flowMonitor); // Schedule next sample
}

// Function to calculate cumulative bytes transferred every 0.0001 seconds and print it to file
static void 
TxTotalBytesSample(Ptr<OutputStreamWrapper> stream, std::vector <Ptr<PacketSink>> sinks)
{
	uint32_t totalBytes = 0;
	for (uint32_t it = 0; it < 6; it++){ // Add bytesTx of 1 ftp app + 5 cbr udp apps 
		totalBytes += sinks[it]->GetTotalRx(); 
	}
	*stream->GetStream() << Simulator::Now().GetSeconds() << ","  << totalBytes << std::endl;
	Simulator::Schedule(Seconds(0.0001), &TxTotalBytesSample, stream, sinks);
}

// Setting TCP protocol using argument passed in the command line
void settingTcpProtocol(std::string transport_prot){
    std::string TcpNewReno("TcpNewReno");
    std::string TcpHybla("TcpHybla");
    std::string TcpScalable("TcpScalable");
    std::string TcpVegas("TcpVegas");
    
    if(transport_prot == TcpNewReno){ 
		Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpNewReno::GetTypeId()));
	}
	else if(transport_prot == TcpHybla){
		Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpHybla::GetTypeId()));
	}
	else if(transport_prot == TcpVegas){
		Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpVegas::GetTypeId()));
	}
	else if(transport_prot == TcpScalable){
		Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpScalable::GetTypeId()));
	}
	else{
		Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpWestwood::GetTypeId()));
	}
}

int main(int argc, char *argv[]){
	uint32_t maxBytes = 0;
	std::string transport_prot = "TcpWestwood";
	std::string cwndTrFileName = "_cwnd.tr";
    std::string pktDropFileName = "_packet_drop.tr";
    std::string bytesTxFileName = "_bytes_tx.tr";

	// Allow the user to override any of the defaults at run-time, via command-line arguments
	CommandLine cmd;
	cmd.AddValue ("transport_prot", "Transport protocol to use: TcpNewReno, "
                "TcpHybla, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat, "
		"TcpLp", transport_prot);
	cmd.Parse(argc, argv);

	// Create Nodes
	NodeContainer nodes;
	nodes.Create(2);            // Two nodes N0 --- N1

	// Create Channels
	// LogComponentEnable ("BulkSendApplication", LOG_LEVEL_ALL); 
    // LogComponentEnable ("OnOffApplication", LOG_LEVEL_ALL); 

	// Explicitly create the point-to-point link required by the topology(shown above).
	PointToPointHelper pointToPoint;
	
	// Using a drop-tail queue at the link 
	pointToPoint.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1p"));
	
	// Setting Bandwidth as 1Mbps
	pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
	
	// Setting link delay as 10ms
	pointToPoint.SetChannelAttribute("Delay", StringValue("10ms"));

	NetDeviceContainer devices;
	devices = pointToPoint.Install(nodes);

	// Setting TCP protocol using argument passed in the command line
	settingTcpProtocol(transport_prot);

	// Install the internet stack on the nodes
	InternetStackHelper internet;
	internet.Install(nodes);

	TrafficControlHelper tch;
  	tch.SetRootQueueDisc ("ns3::PfifoFastQueueDisc", "MaxSize", StringValue ("5p"));
	QueueDiscContainer qdiscs = tch.Install (devices);
	
	// We've got the "hardware" in place. Now we need to add IP addresses.
	// Assign IP Addresses
	Ipv4AddressHelper ipv4;
	ipv4.SetBase("10.1.1.0", "255.255.255.0");
	Ipv4InterfaceContainer i = ipv4.Assign(devices);

	// Create Applications

	uint16_t port = 50000;
	BulkSendHelper source("ns3::TcpSocketFactory",	InetSocketAddress(i.GetAddress(1), port));
	// Set the amount of data to send in bytes.  Zero is unlimited.
	source.SetAttribute("MaxBytes", UintegerValue(maxBytes));
	ApplicationContainer sourceApps = source.Install(nodes.Get(0));

	sourceApps.Start(MilliSeconds(0));
	sourceApps.Stop(MilliSeconds(1800));

	// Create a PacketSinkApplication and install it on node 1
	PacketSinkHelper sink("ns3::TcpSocketFactory",	InetSocketAddress(Ipv4Address::GetAny(), port));
	ApplicationContainer sinkApp = sink.Install(nodes.Get(1));
	
	sinkApp.Start(MilliSeconds(0));
	sinkApp.Stop(MilliSeconds(1800));
	

	// Add 5 constant bit rate sources
	uint16_t cbr_port = 9;
	// Create packet sinks to receive packest from the CBRs
	std::vector<Ptr<PacketSink>> sinks(6);
	sinks[0] = DynamicCast<PacketSink>(sinkApp.Get(0));

	
    // Create CBR1: starts at 200 ms and continues till end
    {
    	OnOffHelper onoff("ns3::UdpSocketFactory", Address(InetSocketAddress(i.GetAddress(1), cbr_port+1)));
    	onoff.SetConstantRate(DataRate("250Kbps"));
    	ApplicationContainer apps = onoff.Install(nodes.Get(0));
    	apps.Start(MilliSeconds(200));
    	apps.Stop(MilliSeconds(1800));
    
    	PacketSinkHelper sink("ns3::UdpSocketFactory",	Address(InetSocketAddress(Ipv4Address::GetAny(), cbr_port+1)));
    	apps = sink.Install(nodes.Get(1));
    	apps.Start(MilliSeconds(200));
    	apps.Stop(MilliSeconds(1800));  
    
    	sinks[1] = DynamicCast<PacketSink>(apps.Get(0));
    }
	
	
	// Create CBR2: starts at 400 ms and continues till end
	{
    	OnOffHelper onoff("ns3::UdpSocketFactory", Address(InetSocketAddress(i.GetAddress(1), cbr_port+2)));
    	onoff.SetConstantRate(DataRate("250Kbps"));
    	ApplicationContainer apps = onoff.Install(nodes.Get(0));
    	apps.Start(MilliSeconds(400));
    	apps.Stop(MilliSeconds(1800));
    
    	PacketSinkHelper sink("ns3::UdpSocketFactory",	Address(InetSocketAddress(Ipv4Address::GetAny(), cbr_port+2)));
    	apps = sink.Install(nodes.Get(1));
    	apps.Start(MilliSeconds(400));
    	apps.Stop(MilliSeconds(1800));  
    
    	sinks[2] = DynamicCast<PacketSink>(apps.Get(0));
	}
	

    // Create CBR3: starts at 600 ms and stops at 1200 ms
    {
    	OnOffHelper onoff("ns3::UdpSocketFactory", Address(InetSocketAddress(i.GetAddress(1), cbr_port+3)));
    	onoff.SetConstantRate(DataRate("250Kbps"));
    	ApplicationContainer apps = onoff.Install(nodes.Get(0));
    	apps.Start(MilliSeconds(600));
    	apps.Stop(MilliSeconds(1200));
    
    	PacketSinkHelper sink("ns3::UdpSocketFactory",	Address(InetSocketAddress(Ipv4Address::GetAny(), cbr_port+3)));
    	apps = sink.Install(nodes.Get(1));
    	apps.Start(MilliSeconds(600));
    	apps.Stop(MilliSeconds(1200));  
    
    	sinks[3] = DynamicCast<PacketSink>(apps.Get(0));
    }
	
	
	// Create CBR4: starts at 800 ms and stops at 1400 ms
	{
    	OnOffHelper onoff("ns3::UdpSocketFactory", Address(InetSocketAddress(i.GetAddress(1), cbr_port+4)));
    	onoff.SetConstantRate(DataRate("250Kbps"));
    	ApplicationContainer apps = onoff.Install(nodes.Get(0));
    	apps.Start(MilliSeconds(800));
    	apps.Stop(MilliSeconds(1400));
    
    	PacketSinkHelper sink("ns3::UdpSocketFactory",	Address(InetSocketAddress(Ipv4Address::GetAny(), cbr_port+4)));
    	apps = sink.Install(nodes.Get(1));
    	apps.Start(MilliSeconds(800));
    	apps.Stop(MilliSeconds(1400));  
    
    	sinks[4] = DynamicCast<PacketSink>(apps.Get(0));
	}
	
	
	// Create CBR5: starts at 1000 ms and stops at 1600 ms
	{
    	OnOffHelper onoff("ns3::UdpSocketFactory", Address(InetSocketAddress(i.GetAddress(1), cbr_port+5)));
    	onoff.SetConstantRate(DataRate("250Kbps"));
    	ApplicationContainer apps = onoff.Install(nodes.Get(0));
    	apps.Start(MilliSeconds(1000));
    	apps.Stop(MilliSeconds(1600));
    
    	PacketSinkHelper sink("ns3::UdpSocketFactory",	Address(InetSocketAddress(Ipv4Address::GetAny(), cbr_port+5)));
    	apps = sink.Install(nodes.Get(1));
    	apps.Start(MilliSeconds(1000));
    	apps.Stop(MilliSeconds(1600));  
    
    	sinks[5] = DynamicCast<PacketSink>(apps.Get(0));
	}
	
	

	// Now, do the actual simulation.
	// Run Simulation
	
    // Enable congestion window sampler
    Simulator::Schedule(Seconds(0.00001), &TraceCwnd, transport_prot+cwndTrFileName);
	
    
	Ptr<FlowMonitor> flowMonitor;
	FlowMonitorHelper flowHelper;
	flowMonitor = flowHelper.InstallAll();

	AsciiTraceHelper ascii;
	Ptr<OutputStreamWrapper> packetDropStream = ascii.CreateFileStream(transport_prot + pktDropFileName);
	// Schedule first instance of sampler function, then it will schedule itself
	Simulator::Schedule(Seconds(0.00001), &packetDropSample, packetDropStream, flowMonitor);

	// Schedule first tx bytes sample function pass its file stream 
	Ptr<OutputStreamWrapper> TxTotalByteStream = ascii.CreateFileStream(transport_prot + bytesTxFileName);
	Simulator::Schedule(Seconds(0.00001), &TxTotalBytesSample, TxTotalByteStream, sinks);
	
	Simulator::Stop(MilliSeconds(1800));
	Simulator::Run();
	
	// Create a flow monitor to obtains stats for monitoting data values
	Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
	std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();

	// Print data values using flow-monitor
	std::cout << std::endl << "*** Flow monitor statistics ***" << std::endl;
	std::cout << "  Tx Packets/Bytes:   " << stats[1].txPackets
			<< " / " << stats[1].txBytes << std::endl;
	std::cout << "  Offered Load: " << stats[1].txBytes * 8.0 / (stats[1].timeLastTxPacket.GetSeconds () - stats[1].timeFirstTxPacket.GetSeconds ()) / 1000000 << " Mbps" << std::endl;
	std::cout << "  Rx Packets/Bytes:   " << stats[1].rxPackets
			<< " / " << stats[1].rxBytes << std::endl;
	uint32_t packetsDroppedByQueueDisc = 0;
	uint64_t bytesDroppedByQueueDisc = 0;
	if (stats[1].packetsDropped.size () > Ipv4FlowProbe::DROP_QUEUE_DISC)
	{
		std::cout << stats[1].packetsDropped.size () << std::endl;
		packetsDroppedByQueueDisc = stats[1].packetsDropped[Ipv4FlowProbe::DROP_QUEUE_DISC];
		bytesDroppedByQueueDisc = stats[1].bytesDropped[Ipv4FlowProbe::DROP_QUEUE_DISC];
	}
	std::cout << "  Packets/Bytes Dropped by Queue Disc:   " << packetsDroppedByQueueDisc
			<< " / " << bytesDroppedByQueueDisc << std::endl;
	uint32_t packetsDroppedByNetDevice = 0;
	uint64_t bytesDroppedByNetDevice = 0;
	if (stats[1].packetsDropped.size () > Ipv4FlowProbe::DROP_QUEUE)
	{
		std::cout << stats[1].packetsDropped.size () << std::endl;
		packetsDroppedByNetDevice = stats[1].packetsDropped[Ipv4FlowProbe::DROP_QUEUE];
		bytesDroppedByNetDevice = stats[1].bytesDropped[Ipv4FlowProbe::DROP_QUEUE];
	}
	std::cout << "  Packets/Bytes Dropped by NetDevice:   " << packetsDroppedByNetDevice
			<< " / " << bytesDroppedByNetDevice << std::endl;
	std::cout << "  Throughput: " << stats[1].rxBytes * 8.0 / (stats[1].timeLastRxPacket.GetSeconds () - stats[1].timeFirstRxPacket.GetSeconds ()) / 1000000 << " Mbps" << std::endl;
	std::cout << "  Mean delay:   " << stats[1].delaySum.GetSeconds () / stats[1].rxPackets << std::endl;
	std::cout << "  Mean jitter:   " << stats[1].jitterSum.GetSeconds () / (stats[1].rxPackets - 1) << std::endl;

  	for(std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i){
		std::cout << " --------------------------------- " << std::endl;
		std::cout << "Flow Id: " << i->first << std::endl;
		std::cout << "Tx Bytes: " << i->second.txBytes  << std::endl;
		std::cout << "Drop Packet Count: " << i->second.lostPackets << std::endl;
	}

	Simulator::Destroy();
    // Simulation Finished
	
}
