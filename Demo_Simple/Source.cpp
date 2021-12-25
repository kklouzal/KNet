#include <KNet.hpp>

int main()
{
    //
    //  Initialize KNet
	KNet::Initialize();

    //
    //  Resolve our send and receive addresses
    auto SendAddr = KNet::AddressPool->GetFreeObject();
    auto RecvAddr = KNet::AddressPool->GetFreeObject();
    SendAddr->Resolve("192.168.1.193", 9000);
    RecvAddr->Resolve("192.168.1.193", 9001);

    //
    //  Create the socket
    KNet::NetPoint* Point = new KNet::NetPoint(SendAddr, RecvAddr);

    //
    //  Send a test packet
    {
        KNet::NetPacket_Send* Pkt = KNet::SendPacketPool->GetFreeObject();
        if (Pkt)
        {
            Pkt->AddDestination(RecvAddr);
            Pkt->write<KNet::PacketID>(KNet::PacketID::Handshake);
            Point->SendPacket(Pkt);
        }
    }

    //
    //  Get any received packets
    const auto Packets1 = Point->GetPackets();
    for (auto _Pkt : Packets1)
    {
        printf("Userland Packet\n");
        Point->ReleasePacket(_Pkt);
    }
    std::system("PAUSE");

    //
    //  Send a bunch of test packets
    for (auto i = 0; i < 1024; i++)
    {
        KNet::NetPacket_Send* Pkt = KNet::SendPacketPool->GetFreeObject();
        if (Pkt)
        {
            Pkt->AddDestination(RecvAddr);
            Pkt->write<KNet::PacketID>(KNet::PacketID::Handshake);
            Point->SendPacket(Pkt);
        }
    }
    std::system("PAUSE");

    //
    //  Get any received packets
    const auto Packets2 = Point->GetPackets();
    for (auto _Pkt : Packets2)
    {
        Point->ReleasePacket(_Pkt);
    }
    std::system("PAUSE");

    //
    //  Send a bunch of test packets
    for (auto i = 0; i < 10; i++)
    {
        KNet::NetPacket_Send* Pkt = KNet::SendPacketPool->GetFreeObject();
        if (Pkt)
        {
            Pkt->AddDestination(RecvAddr);
            Pkt->write<KNet::PacketID>(KNet::PacketID::Handshake);
            Point->SendPacket(Pkt);
        }
    }
    std::system("PAUSE");

    //
    //  Get any received packets
    const auto Packets3 = Point->GetPackets();
    for (auto _Pkt : Packets3)
    {
        Point->ReleasePacket(_Pkt);
    }
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