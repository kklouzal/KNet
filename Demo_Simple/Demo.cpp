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

    for (auto i = 0; i < 256; i++) // 20 iterations ensures packets are being recycled
    {
        //
        //  Send a test packet(s)
        for (auto j = 0; j <= 16; j++)
        {
            KNet::NetPacket_Send* Pkt = KNet::SendPacketPool->GetFreeObject();
            if (Pkt)
            {
                Pkt->AddDestination(RecvAddr);
                //Pkt->write<KNet::PacketID>(KNet::PacketID::Handshake);
                //*Pkt->PID = KNet::PacketID::Handshake;
                Pkt->SetPID(KNet::PacketID::Handshake);
                //Pkt->write<KNet::ClientID>(KNet::ClientID::Client);
                //*Pkt->CID = KNet::ClientID::Client;
                Pkt->SetCID(KNet::ClientID::Client);
                Point->SendPacket(Pkt);
            }
        }

        Sleep(1);

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
        //  Loop all connected clients
        for (auto _Client : ConnectedClients)
        {
            //
            //  Check if each client has any new packets
            const auto Packets = _Client->GetPackets();
            for (auto _Packet : Packets)
            {
                //
                //  Read out the data we sent
                const char* Dat;
                if (_Packet->read<const char*>(Dat))
                {
                    printf("%s\n", Dat);
                }
                //
                //  Release our packet when we're done with it
                Point->ReleasePacket(_Packet);
            }
            //
            //  Send each client a packet on each channel
            auto Pkt1 = _Client->GetFreePacket<KNet::ChannelID::Unreliable_Any>();
            Pkt1->write<const char*>("This is an Unreliable_Any packet");
            Point->SendPacket(Pkt1);
            auto Pkt2 = _Client->GetFreePacket<KNet::ChannelID::Unreliable_Latest>();
            Pkt2->write<const char*>("This is an Unreliable_Latest packet");
            Point->SendPacket(Pkt2);

            auto Pkt4 = _Client->GetFreePacket<KNet::ChannelID::Reliable_Latest>();
            Pkt4->write<const char*>("This is a reliable_Latest packet");
            Point->SendPacket(Pkt4);
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