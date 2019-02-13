#include "Server.h"
#include <pthread.h>
#include <atomic>
#include <chrono>

namespace ospray{
    namespace dw{
        Server *Server::singleton = NULL;
        std::thread Server::commThread;
        std::mutex commThreadIsReady;
        std::mutex canStartProcessing;
        std::mutex displayMutex;
        
        // extern size_t numWrittenThisFrame;
        // extern std::mutex addMutex;
        // size_t Server::numWrittenThisFrame = 0;
        // std::mutex Server::addMutex;

        Server::Server(const int &portNum,
                       const mpicommon::Group &me,
                       const mpicommon::Group &displayGroup,
                       const mpicommon::Group &dispatchGroup,
                       const WallConfig &wallConfig,
                       DisplayCallback displayCallback,
                       void *objectForCallback,
                       const bool &hasHeadNode,
                       const int &ppn,
                       const int clientNum,
                       const int image_socket)
            :portNum(portNum), me(me), displayGroup(displayGroup),
             dispatchGroup(dispatchGroup), wallConfig(wallConfig),
             displayCallback(displayCallback), objectForCallback(objectForCallback),
             hasHeadNode(hasHeadNode), ppn(ppn), numExpectedThisFrame(wallConfig.totalPixels().product()),
              numExpectedPerDisplay( wallConfig.displayPixelCount()), recv_l(NULL), recv_r(NULL), disp_l(NULL), disp_r(NULL), clientNum(clientNum),
             numPixelsPerClient(wallConfig.calculateNumPixelsPerClient(clientNum)), numWrittenThisClient(std::vector<int>(clientNum, 0)),
             quit_threads(false), currFrameID(1), recvNumTiles(0), 
             numTilesPerFrame(wallConfig.calculateNumTilesPerFrame()),image_socket(image_socket)
        {
            // commThreadIsReady.lock();
            // canStartProcessing.lock();
            // commThread = std::thread([this](){
                    setupCommunication();
            // });

            // commThreadIsReady.lock();

            // if(hasHeadNode && me.rank == 0){
            //     commThread.join();
            // }
            // canStartProcessing.unlock();
        }

        Server::~Server(){
            // for(int i = 0; i < clientNum; i++){
            //     recvThread[i].join();
            // }
        }

        void Server::setupCommunication()
        {
            const mpicommon::Group &world = me;
            //std::cout << "Using TCP/IP port" << std::endl;
            //std::cout << "debug display Group size = " << displayGroup.size << std::endl;
            //std::cout << "debug dispatch Group size = " << dispatchGroup.size << std::endl;
            if(hasHeadNode){
                // =======================================================
                // DISPATCHER
                // =======================================================
                if(world.rank == 0){
                    waitForConnection(portNum);
                    // FD_ZERO(&readfds);
                    for(int i = 0; i < clientNum; i++){
                        int rank = -1;
                        int sock = client_socket[i];
                        int r = recv(sock, &rank, sizeof(int), 0);
                        rankList.push_back(rank);
                        std::cout << " socket #" << sock << " rank #" << rank << std::endl;
                    }
                    // runDispatcher();
                    receiveImage();
                }else{
                    // =======================================================
                    // TILE RECEIVER
                    // =======================================================
                    //  canStartProcessing.lock();
                    mpicommon::Group incomingTiles = dispatchGroup;
                    processIncomingTiles(incomingTiles);
                }
            }
        }

        void Server::waitForConnection(const int &portNum)
        {
            char portName[MPI_MAX_PORT_NAME];
            MPI_Comm outside;
            std::cout << std::endl;
            int opt = 1;
            int activity;
            int new_socket;
            
            // Set of socket descriptors
            addrlen = sizeof(address);
            // Creating socket file descriptor
            if ((service_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0){
                perror("socket failed");
                exit(EXIT_FAILURE);
            }
            // Forcefully attaching socket to the port 8080
            if (setsockopt(service_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
            {
                perror("setsockopt");
                exit(EXIT_FAILURE);
            }
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(portNum);
            // Forcefully attaching socket to the port 8080
            if (bind(service_sock, (struct sockaddr *)&address, sizeof(address))<0)
            {
                perror("bind failed");
                exit(EXIT_FAILURE);
            }
            if (listen(service_sock, 30) < 0)
            {
                perror("listen");
                exit(EXIT_FAILURE);
            }

            for (int i = 0; i < max_clients; i++)
            {
                client_socket[i] = 0;
            }
            // Accept incoming connections
            printf("Waiting for connections ... \n");
            int  c = 0;
            while(1)
            {
                if ((new_socket = accept(service_sock, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0)  
                {
                    perror("accept");  
                    exit(EXIT_FAILURE);  
                } 
                //inform user of socket number - used in send and receive commands 
                printf("New connection , socket fd is %d , ip is : %s , port : %d \n" , new_socket , inet_ntoa(address.sin_addr) , ntohs
                        (address.sin_port));
                //add new socket to array of sockets 
                for (int i = 0; i < clientNum; i++)  {  
                    //if position is empty 
                    if( client_socket[i] == 0) 
                    {
                        client_socket[i] = new_socket;
                        printf("Adding to list of sockets as %d\n" , i);
                        c = i;
                        break;
                    }
                }
                if(c == clientNum - 1){
                    break;
                }
            } // while
            dispatchGroup.barrier();
        }

        void Server::receiveImage()
        {
            void *decompressor = CompressedTile::createDecompressor();
            allocateImageFrameBuffer();
            
            for(int i = 0; i < 3; i++){
                CompressedTile image;
                int numBytes = 0;
                int dataSize = recv(image_socket, &numBytes, sizeof(int), 0);
                image.numBytes = numBytes;
                image.data = new unsigned char[image.numBytes];
                valread = recv(image_socket , image.data, image.numBytes, MSG_WAITALL);
                std::cout << "receive " << numBytes << " bytes " << valread << std::endl;
                // decompression
                const CompressedTileHeader *header = (const CompressedTileHeader *)image.data;
                PlainTile plain(image.getRegion().size());
                image.decode(decompressor,plain);
                if(plain.eye){
                    image_recv_r = plain.pixel;
                }else{
                    image_recv_l = plain.pixel;
                }
                displayCallback(image_recv_l,image_recv_r, objectForCallback);
                // DEBUG
                vec2i img_size{512, 512};
                writePPM("test.ppm", &img_size, image_recv_l);
                std::cout << "Image saved to 'test.ppm'\n";

                std::swap(image_recv_l,image_disp_l);
                std::swap(image_recv_r,image_disp_r);
            }
        }


        void Server::allocateFrameBuffers()
        {
          assert(recv_l == NULL);
          assert(disp_l == NULL);
          assert(recv_r == NULL);
          assert(disp_r == NULL);
          const int pixelsPerBuffer = wallConfig.pixelsPerDisplay.product();
          recv_l = new uint32_t[pixelsPerBuffer];
          disp_l = new uint32_t[pixelsPerBuffer];
          if (wallConfig.stereo) {
            recv_r = new uint32_t[pixelsPerBuffer];
            disp_r = new uint32_t[pixelsPerBuffer];
          }
        }// end of allocateFrameBuffers

        void Server::allocateImageFrameBuffer()
        {
          assert(image_recv_l == NULL);
          assert(image_disp_l == NULL);
          assert(image_recv_r == NULL);
          assert(image_disp_r == NULL);

          const int pixelsPerBuffer = wallConfig.pixelsPerDisplay.product();
          image_recv_l = new uint32_t[pixelsPerBuffer];
          image_disp_l = new uint32_t[pixelsPerBuffer];
          if (wallConfig.stereo) {
            image_recv_r = new uint32_t[pixelsPerBuffer];
            image_disp_r = new uint32_t[pixelsPerBuffer];
          }
        }// end of allocateFrameBuffers


        void startWallServer(const mpicommon::Group world,
                                    const mpicommon::Group displayGroup,
                                    const mpicommon::Group dispatchGroup,
                                    const WallConfig &wallConfig,
                                    bool hasHeadNode,
                                    DisplayCallback displayCallback,
                                    void *objectForCallback,
                                    int portNum,
                                    int process_pernode,
                                    int clientNum,
                                    int image_socket){
            assert(Server::singleton == NULL);
            Server::singleton = new Server(portNum, world, displayGroup, dispatchGroup, wallConfig, displayCallback, objectForCallback, hasHeadNode, process_pernode, clientNum, image_socket);
        }

        void Server::endFramePrint()
        {
            compressionStats stats(compressions);
            stats.time_suffix = "%";
            std::cout << "Compression ratio statistics: \n" << stats << "\n";

            if(!recvtimes.empty()){
                Stats recvStats(recvtimes);
                recvStats.time_suffix = "ms";
                std::cout  << "Message recv statistics:\n" << recvStats << "\n";
            }
            
            if(!decompressiontimes.empty()){
                Stats decompressionStats(decompressiontimes);
                decompressionStats.time_suffix = "ms";
                std::cout  << "Decompression time statistics:\n" << decompressionStats << "\n";
            }
           
        }
 

}// end of ospray::dw
}// end of ospray
