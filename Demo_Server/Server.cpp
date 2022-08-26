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
                    //printf("%s\n", Dat);
                }
                //
                //  Release our packet when we're done with it
                Point->ReleasePacket(_Packet);
            }
        }
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