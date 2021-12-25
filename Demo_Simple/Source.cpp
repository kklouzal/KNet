#include <KNet.hpp>

int main()
{
    //
    //  Initialize KNet
	KNet::Initialize();
	std::system("PAUSE");

    //
    //  Resolve our send and receive addresses
    auto SendAddr = KNet::AddressPool->GetFreeObject();
    auto RecvAddr = KNet::AddressPool->GetFreeObject();
    SendAddr->Resolve("192.168.1.193", 9000);
    RecvAddr->Resolve("192.168.1.193", 9001);
	std::system("PAUSE");

    //
    //  Create the socket
    KNet::NetPoint* Point = new KNet::NetPoint(SendAddr, RecvAddr);
    std::system("PAUSE");

    //
    //  Send a test packet
    {
        KNet::NetPacket_Send* Pkt = KNet::SendPacketPool->GetFreeObject();
        Pkt->AddDestination(RecvAddr);
        Pkt->write<KNet::PacketID>(KNet::PacketID::Handshake);
        Point->SendPacket(Pkt);
    }
    std::system("PAUSE");

    //
    //  Get any received packets
    auto Pkts1 = Point->GetPackets();
    std::system("PAUSE");

    //
    //  Send a bunch of test packets
    for (auto i = 0; i < 1024; i++)
    {
        KNet::NetPacket_Send* Pkt = KNet::SendPacketPool->GetFreeObject();
        Pkt->AddDestination(RecvAddr);
        Pkt->write<KNet::PacketID>(KNet::PacketID::Handshake);
        Point->SendPacket(Pkt);
    }
    std::system("PAUSE");

    //
    //  Get any received packets
    auto Pkts2 = Point->GetPackets();
    std::system("PAUSE");

    //
    //  Send a bunch of test packets
    for (auto i = 0; i < 10; i++)
    {
        KNet::NetPacket_Send* Pkt = KNet::SendPacketPool->GetFreeObject();
        Pkt->AddDestination(RecvAddr);
        Pkt->write<KNet::PacketID>(KNet::PacketID::Handshake);
        Point->SendPacket(Pkt);
    }
    std::system("PAUSE");

    //
    //  Get any received packets
    auto Pkts3 = Point->GetPackets();
    std::system("PAUSE");

    //
    //  Delete the socket
    delete Point;
    std::system("PAUSE");

    //
    //  Deinitialize the library
	KNet::Deinitialize();
	return 0;
}