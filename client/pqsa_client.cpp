#include "pqsa.hpp"

int s, err;
int len_inet;              // length

char* port;
char* srvr_addr;
unsigned int delay = 0;

bool terminate = false;

struct sockaddr_in adr_srvr;
struct sockaddr_in adr;

typedef struct {
  udp_packet_t *pdu;
  long long seconds;
  long long millis;
} sendPkt;

pthread_t sending_tid;

std::vector<sendPkt *> sendingList;
pthread_mutex_t lockSendingList;

static void displayError(const char *on_what) {
  fputs(strerror(errno),stderr);
  fputs(": ",stderr);
  fputs(on_what,stderr);
  fputc('\n',stderr);
  exit(1);
}

void* sending_thread(void *arg)
{
    int z = 0;
    struct timeval timestamp;
    sendPkt *pkt;

    while (!terminate) {

        pthread_mutex_lock(&lockSendingList);

        if (sendingList.size() > 0) {
            pkt = *sendingList.begin();
            z = sendto(s, pkt->pdu, sizeof(udp_packet_t), 0, (struct sockaddr *)&adr_srvr, len_inet);
            free(pkt->pdu);

            if (z < 0) {
                if (errno == ENOBUFS||errno == EAGAIN||errno == EWOULDBLOCK)
                    std::cout << "reached maximum OS UDP buffer size\n";
                else
                    displayError("sendto(2)");

	        	pthread_mutex_unlock(&lockSendingList);
				continue;
	    	}	

            sendingList.erase(sendingList.begin());
        }

        pthread_mutex_unlock(&lockSendingList);
		usleep(0.01);
    }

    return NULL;
}

int main(int argc, char **argv) 
{
    int z;
    int i = 1;
    char command[512];
    char tmp[512];

    udp_packet_t *pdu;
    sendPkt *pkt;
    struct timeval timestamp;
    setbuf(stdout, NULL);
    std::ofstream clientLog;

    pthread_mutex_init(&lockSendingList, NULL);

    if (argc < 4) {
        std::cout << "syntax should be ./pqsa_client <server address> -p <server port> [-d <additional link delay in ms>] \n";
        exit(0);
  }

    srvr_addr = argv[1];

    while (i != (argc-1)) { // Check that we haven't finished parsing already
        i = i + 1;
        if (!strcmp (argv[i], "-p")) {
            i = i + 1;
            port = argv[i];
        } else if (!strcmp (argv[i], "-d")) {
            i = i + 1;
            delay = atoi(argv[i]);
        } else {
            std::cout << "syntax should be ./pqsa_client <server address> -p <server port> [-d <additional link delay in ms>] \n";
            exit(0);
        }
    }

    memset(&adr_srvr, 0, sizeof adr_srvr);

    adr_srvr.sin_family = AF_INET;
    adr_srvr.sin_port = htons(atoi(port));
    adr_srvr.sin_addr.s_addr =  inet_addr(srvr_addr);

    if ( adr_srvr.sin_addr.s_addr == INADDR_NONE ) {
        displayError("bad address.");
    }

    len_inet = sizeof adr_srvr;

    s = socket(AF_INET,SOCK_DGRAM,0);
    if ( s == -1 ) {
        displayError("socket()");
    }

    std::cout << "Sending request to server \n";

    z = sendto(s,"Hallo", strlen("Hallo"), 0, (struct sockaddr *)&adr_srvr, len_inet);
    if ( z < 0 )
        displayError("sendto(Hallo)");

    socklen_t len = sizeof(struct sockaddr_in);
    z = -1;

    if (pthread_create(&(sending_tid), NULL, &sending_thread, NULL) != 0)
        std::cout << "can't create thread: " << strerror(err) << "\n";

    /* starting to loop waiting to receive data and to ACK */
    while(!terminate) {
        pdu = (udp_packet_t *) malloc(sizeof(udp_packet_t));
        z = recvfrom(s, pdu, sizeof(udp_packet_t), 0, (struct sockaddr *)&adr, &len);
        if ( z < 0 )
            displayError("recvfrom(2)");

        if (pdu->ss_id < 0) {
            clientLog.close();
            terminate = true;
        }
        gettimeofday(&timestamp, NULL);
        pkt = (sendPkt *)malloc(sizeof(sendPkt));
        pkt->pdu = pdu;
        pkt->seconds = timestamp.tv_sec;
        pkt->millis = timestamp.tv_usec;

        pthread_mutex_lock(&lockSendingList);
        sendingList.push_back(pkt);
        pthread_mutex_unlock(&lockSendingList);
    }

    std::cout << "Client exiting \n";
    close(s);
    return 0;
}
