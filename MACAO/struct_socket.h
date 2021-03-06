/*
 * struct_socket.h
 *
 *      Author:  thanghoang
 */
#ifndef STRUCT_SOCKET_H
#define STRUCT_SOCKET_H

struct struct_socket
{
	std::string ADDR;

	unsigned char *data_out;
	size_t data_out_size;
	bool isSend;
	unsigned char *data_in;
	size_t data_in_size;
	int peer_idx;

	int CMD;
    
 /**
 *
 * @param peer_idx: idx of the peer to be connected
 * @param data_out: data to be sent
 * @param data_out_size: length of data to be sent
 * @param data_in: data to be received
 * @param data_in_size: length of data to be received
 * @param CMD: Command (see config.h)
 * @param isSend: whether the connect is send only
 *
 * @return 0 if successful
 */
	struct_socket(int peer_idx, unsigned char *data_out, size_t data_out_size, unsigned char *data_in, size_t data_in_size, int CMD, bool isSend)
	{
		this->peer_idx = peer_idx;

		this->data_out = data_out;
		this->data_out_size = data_out_size;

		this->CMD = CMD;
		this->data_in = data_in;
		this->data_in_size = data_in_size;

		this->isSend = isSend;
	}
	struct_socket()
	{
	}
	~struct_socket()
	{
	}
};

#endif // STRUCT_SOCKET_H
