#define _CRT_SECURE_NO_WARNINGS
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
    SendAddr->Resolve("192.168.1.98", 9000);
    RecvAddr->Resolve("192.168.1.98", 9001);

    //
    //  Create the socket
    KNet::NetPoint* Point = new KNet::NetPoint(SendAddr, RecvAddr);


    auto RemoteAddr = KNet::AddressPool->GetFreeObject();
    RemoteAddr->Resolve("96.38.165.24", 27015);

    std::system("PAUSE");
    //
    //  Hold onto any connected clients
    std::deque<KNet::NetClient*> ConnectedClients;

    for (auto i = 0; i < 512; i++) // 20 iterations ensures packets are being recycled
    {
        //
        //  Send a test packet(s)
        for (auto j = 0; j < 1; j++)
        {
            KNet::NetPacket_Send* Pkt = KNet::SendPacketPool->GetFreeObject();
            if (Pkt)
            {
                Pkt->AddDestination(RemoteAddr);
                Pkt->SetPID(KNet::PacketID::Handshake);
                Pkt->SetCID(KNet::ClientID::Client);
                Point->SendPacket(Pkt);
            }
        }

        Sleep(1);

        //
        //  Get any received out-of-band packets
        const auto Packets1 = Point->GetPackets();
        for (auto _Packet : Packets1.Packets)
        {
            //
            //  Release our packet when we're done with it
            Point->ReleasePacket(_Packet);
        }
        //
        //  Check for new clients
        for (auto _Client : Packets1.Clients_Connected)
        {
            ConnectedClients.push_back(_Client);
            //
            //  Register your custom Operation IDs
            _Client->RegisterChannel<KNet::ChannelID::Unreliable_Any>(0);
            _Client->RegisterChannel<KNet::ChannelID::Unreliable_Latest>(1);
            _Client->RegisterChannel<KNet::ChannelID::Reliable_Any>(2);
            _Client->RegisterChannel<KNet::ChannelID::Reliable_Latest>(3);
            _Client->RegisterChannel<KNet::ChannelID::Reliable_Ordered>(4);
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
            KNet::NetPacket_Send* Pkt1 = _Client->GetFreePacket(0);
            if (Pkt1) {
                Pkt1->write<const char*>("This is an Unreliable_Any packet");
                Point->SendPacket(Pkt1);
            } else { printf("PKT1 UNAVAILABLE!\n"); }
            KNet::NetPacket_Send* Pkt2 = _Client->GetFreePacket(1);
            if (Pkt2) {
                Pkt2->write<const char*>("This is an Unreliable_Latest packet");
                Point->SendPacket(Pkt2);
            }
            else { printf("PKT2 UNAVAILABLE!\n"); }
            KNet::NetPacket_Send* Pkt3 = _Client->GetFreePacket(2);
            if (Pkt3) {
                Pkt3->write<const char*>("This is a Reliable_Any packet");
                Point->SendPacket(Pkt3);
            }
            else { printf("PKT3 UNAVAILABLE!\n"); }
            KNet::NetPacket_Send* Pkt4 = _Client->GetFreePacket(3);
            if (Pkt4) {
                Pkt4->write<const char*>("This is a Reliable_Latest packet");
                Point->SendPacket(Pkt4);
            }
            else { printf("PKT4 UNAVAILABLE!\n"); }
            KNet::NetPacket_Send* Pkt5 = _Client->GetFreePacket(4);
            if (Pkt5) {
                Pkt5->write<const char*>("This is a Reliable_Ordered packet");
                Point->SendPacket(Pkt5);
            }
            else { printf("PKT5 UNAVAILABLE!\n"); }
        }
        //
        //  Check for disconnected clients
        //  NOTE: The same NetClient can wind up in the Clients_Disconnected deque multiple times.
        //  Use some external means to identify if they've been cleaned up/marked for cleanup.
        for (auto _Client : Packets1.Clients_Disconnected)
        {
            for (std::deque<KNet::NetClient*>::iterator it = ConnectedClients.begin(); it != ConnectedClients.end();)
            {
                if (*it == _Client)
                {
                    ConnectedClients.erase(it);
                    Point->ReleaseClient(_Client);
                    printf("DISCONNECT CLIENT\n");
                    break;
                }
            }
        }
        //
        //  Run a Timeout Check
        Point->CheckForTimeouts();
        //std::system("PAUSE");
    }

    //
    //  Delete the socket
    delete Point;

    //
    //  Deinitialize the library
	KNet::Deinitialize();
	return 0;
}