#include "cloud_client.hpp"
#define  STORE_FILE "./list.backup"
#define  LISTEN_DIR "./backup/"//����һ��Ŀ¼
#define  SERVER_IP "62.234.130.58"
#define  SERVER_PORT 9000 
int main()
{
	CloudClient client(LISTEN_DIR, STORE_FILE, SERVER_IP, SERVER_PORT);
	client.Start();
	return 0;
}