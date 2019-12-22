/*
 * ServerBinaryORAMO.cpp
 *
 *      Author: thanghoang
 */

#include "ServerBinaryORAMO.hpp"
#include "Utils.hpp"
#include "struct_socket.h"

#include "ORAM.hpp"
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>

#include "struct_thread_computation.h"
#include "struct_thread_loadData.h"






ServerBinaryORAMO::ServerBinaryORAMO(TYPE_INDEX serverNo, int selectedThreads) : ServerORAM(serverNo, selectedThreads)
{
	
    this->write_root_in = new unsigned char[BLOCK_SIZE*2 + sizeof(TYPE_INDEX)];
    this->client_write_root_in = new unsigned char[BLOCK_SIZE*2];
    
    
}

ServerBinaryORAMO::ServerBinaryORAMO()
{
}

ServerBinaryORAMO::~ServerBinaryORAMO()
{
}


int ServerBinaryORAMO::prepareEvictComputation()
{
    unsigned long long currBufferIdx = 0;
    //evict matrix
    for (TYPE_INDEX y = 0 ; y < H+1 ; y++)
    {
        for (TYPE_INDEX i = 0 ; i < EVICT_MAT_NUM_ROW; i++)
        {
            memcpy(this->vecEvictMatrix[0][y][i], &evict_in[currBufferIdx], EVICT_MAT_NUM_COL*sizeof(TYPE_DATA));
            
            memcpy(this->vecEvictMatrix[1][y][i], &client_evict_in[currBufferIdx], EVICT_MAT_NUM_COL*sizeof(TYPE_DATA));
            currBufferIdx += EVICT_MAT_NUM_COL*sizeof(TYPE_DATA);
        }
    }
}

/**
 * Function Name: evict
 *
 * Description: Starts eviction operation with the command of the client by receiving eviction matrix
 * and eviction path no from the client. According to eviction path no, the server performs 
 * matrix multiplication with its buckets and eviction matrix to evict blocks. After eviction operation,
 * the degree of the sharing polynomial doubles. Thus all the servers distributes their shares and perform 
 * degree reduction routine simultaneously. 
 * 
 * @param socket: (input) ZeroMQ socket instance for communication with the client
 * @return 0 if successful
 */  
int ServerBinaryORAMO::evict(zmq::socket_t& socket)
{
    
    recvClientEvictData(socket);
    
    prepareEvictComputation();
    
    TYPE_INDEX srcIdx[H];
    TYPE_INDEX destIdx[H];
    TYPE_INDEX siblIdx[H];
    
    string evict_str = ORAM::getEvictString(n_evict);
    ORAM::getEvictIdx(srcIdx,destIdx,siblIdx,evict_str);
    
    for(int h = 0; h < H+1 ; h++)
    {
        cout<<endl;
		cout << "	==============================================================" << endl;
		cout<< "	[evict] Starting TripletEviction-" << h+1 <<endl;
        
        //== THREADS FOR LISTENING =======================================================================================
        cout<< "	[evict] Creating Threads for Receiving Ports..." << endl;
        for(TYPE_INDEX k = 0; k < NUM_SERVERS-1; k++)
        {
            recvSocket_args[k] = struct_socket(k, NULL, 0, reshares_in[k], SERVER_RESHARE_IN_OUT_LENGTH, NULL,false);
            pthread_create(&thread_recv[k], NULL, &ServerORAM::thread_socket_func, (void*)&recvSocket_args[k]);
        }
        
        
		TYPE_INDEX curSrcIdx = srcIdx[h];
        TYPE_INDEX curDestIdx = destIdx[h];
        if(h == H) //for src-to-sibling bucket operation at leaf level
        {
            curSrcIdx = srcIdx[H-1];
            curDestIdx = siblIdx[H-1];
        }
        // LOAD BUCKETS FROM DISK
		auto start = time_now;
		
        TYPE_ID readBucketIDS[2] = {curSrcIdx, curDestIdx};
        
        this->readBucket_evict_reverse(readBucketIDS,this->serverNo,this->vecEvictPath_db[0],this->vecEvictPath_MAC[0]);
        this->readBucket_evict_reverse(readBucketIDS,(this->serverNo+1)%3,this->vecEvictPath_db[1],this->vecEvictPath_MAC[1]);
        
		auto end = time_now;
        
		long load_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
		cout<< "	[evict] Evict Nodes READ from Disk in " << load_time <<endl;
		server_logs[7] += load_time;
        
        //perform matrix product
        cout<< "	[evict] Multiplying Evict Matrix..." << endl;
		start = time_now;
		this->preReSharing(h,0,1); 	// SERVER SIDE COMPUTATION
        end = time_now;
		cout<< "	[evict] Multiplied in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count() <<endl;
		server_logs[8] += std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();

		
        
          start = time_now;
        cout<< "	[evict] ReSharing..." << endl;
        this->reShare(h,0,1);
        end = time_now;
        cout<< "	[evict] Reshared CREATED in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count() <<endl;
        server_logs[9] += std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
    
		//== THREADS FOR SENDING ============================================================================================
		cout<< "	[evict] Creating Threads for Sending Shares..."<< endl;;
		for (int i = 0; i < NUM_SERVERS-1; i++)
		{
			sendSocket_args[i] = struct_socket(i,  reshares_out[i], SERVER_RESHARE_IN_OUT_LENGTH, NULL, 0, NULL, true);
			pthread_create(&thread_send[i], NULL, &ServerORAM::thread_socket_func, (void*)&sendSocket_args[i]);
		}
		cout<< "	[evict] CREATED!" <<endl;
		//=================================================================================================================
		cout<< "	[evict] Waiting for Threads..." <<endl;
		for (int i = 0; i < NUM_SERVERS-1; i++)
		{
			pthread_join(thread_send[i], NULL);
			pthread_join(thread_recv[i], NULL);
		}
		cout<< "	[evict] DONE!" <<endl;
        server_logs[10] += thread_max;
		thread_max = 0;
		
		     
        start = time_now;
        cout << "	[evict] Post Resharing Computation..." << endl; 
        
        this->postReSharing(h,0,1);
   
        end = time_now;
        server_logs[11] += std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
        
        //write to file
        start = time_now;
        FILE* file_out = NULL;
        string path;
        
        //overwrite (non-leaf) sibling bucket with source bucket
        if(h < H-1)
        {
            this->copyBucket(this->serverNo,curSrcIdx,siblIdx[h]);
            this->copyBucket((this->serverNo+1)%3,curSrcIdx,siblIdx[h]);
        }
        
        this->writeBucket_reverse_mode(curDestIdx,serverNo,vecReShares[0][serverNo],vecReShares_MAC[0][serverNo]);
        this->writeBucket_reverse_mode(curDestIdx,(serverNo+1)%3,vecReShares[0][(serverNo+1)%3],vecReShares_MAC[0][(serverNo+1)%3]);
   
        end = time_now;
        server_logs[12] += std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
        
        cout<< "	[evict] Reduction DONE in " << server_logs[11] <<endl;
        cout<< "	[evict] Written to Disk in " << server_logs[12] <<endl;
		cout<< "	[evict] TripletEviction-" << h+1 << " COMPLETED!"<<endl;
    }
    
    socket.send((unsigned char*)CMD_SUCCESS,sizeof(CMD_SUCCESS));
	cout<< "	[evict] ACK is SENT!" <<endl;

    return 0;
}





int ServerBinaryORAMO::readBucket_evict_reverse(TYPE_ID bucketIDs[], int shareID, zz_p** output_data, zz_p** output_mac)
{
    
    FILE* file_in = NULL;
    FILE* file_in_mac = NULL;
    
    string path, path_mac;
    for(int s = 0 ; s < 2; s++)
    {
        path = myStoragePath + to_string(shareID) + "/" + to_string(bucketIDs[s]);
        if((file_in = fopen(path.c_str(),"rb")) == NULL)
        {
            cout<< path << " cannot be opened!!" <<endl;
            exit;
        }
        for(int i = 0 ; i < BUCKET_SIZE; i++)
        {
            for(int j = 0 ; j < DATA_CHUNKS; j++)
            {
                fread(&output_data[j][s*BUCKET_SIZE+i], 1, sizeof(TYPE_DATA), file_in);
            }
        }
        fclose(file_in);

        path_mac  = path + "_mac";
        if((file_in_mac = fopen(path_mac.c_str(),"rb")) == NULL)
        {
            cout<< path_mac << " cannot be opened!!" <<endl;
            exit;
        }
        for(int i = 0 ; i < BUCKET_SIZE; i++)
        {
            for(int j = 0 ; j < DATA_CHUNKS; j++)
            {
                fread(&output_mac[j][s*BUCKET_SIZE+i], 1, sizeof(TYPE_DATA), file_in_mac);
            }
        }
        fclose(file_in_mac);
    }
}