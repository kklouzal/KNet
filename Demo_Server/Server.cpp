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

    //
    //  Hold onto any connected clients
    std::deque<KNet::NetClient*> ConnectedClients;

    while (true)
    {
        //
        //  Get any received out-of-band packets
        const auto& Packets1 = Point->GetPackets();
        for (auto& _Packet : Packets1.Packets)
        {
            //
            //  Release our packet when we're done with it
            Point->ReleasePacket(_Packet);
        }
        //
        //  Check for new clients
        for (auto& _Client : Packets1.Clients_Connected)
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
        for (auto& _Client : ConnectedClients)
        {
            //
            //  Check if each client has any new packets
            const auto& Packets = _Client->GetPackets();
            //bool nl = false;
            for (auto& _Packet : Packets)
            {
                //nl = true;
               // printf("Packet OpID: %i, UID: %ju\n", _Packet->GetOID(), _Packet->GetUID());
                //
                //  Read out the data we sent
                //const char* Dat;
                //if (_Packet->read<const char*>(Dat))
                //{
                //    printf("%s\n", Dat);
                //}
                //
                //  Release our packet when we're done with it
                Point->ReleasePacket(_Packet);
            }
            //if (nl)
            //printf("\n");
        }
        //
        //  Check for disconnected clients
        //  NOTE: The same NetClient can wind up in the Clients_Disconnected deque multiple times.
        //  Use some external means to identify if they've been cleaned up/marked for cleanup.
        for (auto& _Client : Packets1.Clients_Disconnected)
        {
            printf("DISCONNECT CLIENT\n");
            for (std::deque<KNet::NetClient*>::iterator it = ConnectedClients.begin(); it != ConnectedClients.end();)
            {
                if (*it == _Client)
                {
                    ConnectedClients.erase(it);
                    Point->ReleaseClient(_Client);
                    break;
                }
            }
        }
        //
        //  Run a Timeout Check
        Point->CheckForTimeouts();
        Sleep(1);
    }

    //
    //  Delete the socket
    delete Point;

    //
    //  Deinitialize the library
    KNet::Deinitialize();
    return 0;
}