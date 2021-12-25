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
    //  Hold onto any connected clients
    std::deque<KNet::NetClient*> ConnectedClients;

    for (auto i = 0; i < 20; i++) // 20 iterations ensures packets are being recycled
    {
        //
        //  Send a test packet(s)
        for (auto j = 1; j <= i*i; j++)
        {
            KNet::NetPacket_Send* Pkt = KNet::SendPacketPool->GetFreeObject();
            if (Pkt)
            {
                Pkt->AddDestination(RecvAddr);
                Pkt->write<KNet::PacketID>(KNet::PacketID::Handshake);
                Pkt->write<KNet::ClientID>(KNet::ClientID::Client);
                Point->SendPacket(Pkt);
            }
        }

        std::system("PAUSE");

        //
        //  Get any received out-of-band packets
        const auto Packets1 = Point->GetPackets();
        for (auto _Packet : Packets1.first)
        {
            //
            //  Release our packet when we're done with it
            Point->ReleasePacket(_Packet);
        }
        //
        //  Check for new clients
        for (auto _Client : Packets1.second)
        {
            ConnectedClients.push_back(_Client);
            printf("HANDLE NEW CLIENT\n");
        }
        //
        //  Loop all connected clients for client specific packets
        for (auto _Client : ConnectedClients)
        {
            const auto Packets = _Client->GetPackets();
            for (auto _Packet : Packets)
            {
                //
                //  Release our packet when we're done with it
                Point->ReleasePacket(_Packet);
            }
        }
        std::system("PAUSE");
    }

    //
    //  Delete the socket
    delete Point;

    //
    //  Deinitialize the library
	KNet::Deinitialize();
	return 0;
}