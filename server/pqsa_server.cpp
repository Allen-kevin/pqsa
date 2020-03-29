#include "pqsa.hpp"
#include <queue>
/* hybrid slow start */
#define HYSTART_ACK_TRAIN   0x1
#define HYSTART_DELAY       0x2
#define HYSTART_DELAY_MIN   (4u<<3)
#define HYSTART_DELAY_MAX   (16u<<3)

#define HYSTART_DELAY_THRESH clamp(x, HYSTART_DELAY_MIN, HYSTART_DELAY_MAX)
static uint8_t hystart_detect = HYSTART_ACK_TRAIN | HYSTART_DELAY;
static int hystart_low_window  = 16;
static uint32_t hystart_ack_delta = 2;
/* end */

#define MTU 1500
#define PACKET_SIZE MTU
#define DELTA 2

#define T_THRESHOLD 20    //parameter
#define RESOLUTION 1000
/* state val */
bool rtt_detect = true;
bool terminate = false;
bool slow_start = true;

/* socket id */
int s, err;

/* session id */
int ss_id;

int M = 3;   //parameter
int tau = 3;  //parameter

uint32_t crt_win = 1;
uint32_t sender_pact = 2;
uint32_t ss_crt_win = 1;
int slow_epoch = 1;
socklen_t len_inet;

double alpha = 0.8;
uint32_t cur_rtt = UINT_MAX;
uint32_t latest_rtt = UINT_MAX;
uint32_t min_rtt = UINT_MAX;
double sending_rate;
double sending_rate_latest;
double s_gamma;
double sigma = 0.5;

/* packet sequence */
int pkt_seq;
int rcv_bytes;

struct hybridss s_hyss;

struct sockaddr_in adr_clnt;
struct sockaddr_in adr_clnt2; 

/* the time of program start run */
struct timeval start_time;
struct timeval last_ack_time;

std::queue<uint32_t> t_win;

pthread_mutex_t sendingWinLock;
pthread_mutex_t sendingCntLock;
pthread_mutex_t restartLock;

pthread_t timeout_tid;
pthread_t sending_tid;
pthread_t receiver_tid;

boost::asio::io_service io;
boost::asio::deadline_timer timer (io, boost::posix_time::milliseconds(DELTA*M));

uint64_t Clamp(uint64_t val)
{
    if (val > HYSTART_DELAY_MAX)
        return HYSTART_DELAY_MAX;
    else if (val < HYSTART_DELAY_MIN)
        return HYSTART_DELAY_MIN;
    else
        return val;
}

static inline uint32_t pqsa_clock(void)
{
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    return current_time.tv_sec*1000 + current_time.tv_usec;
#if 0
#if HZ < 1000
        return ktime_to_ms(ktime_get_real());
#else
        return jiffies_to_msecs(jiffies);
#endif
#endif
}

static void hystart_reset(struct hybridss *hyss)
{
    hyss->round_start = hyss->last_ack = pqsa_clock();
	
	FILE *fp;
	double run_time = 0;
	struct timeval current_time;
	gettimeofday(&current_time,NULL);
	run_time = (current_time.tv_sec - start_time.tv_sec) + (current_time.tv_usec - start_time.tv_usec)/1000000.0;
	fp = fopen("einfo_slow_exit.txt", "a");	
	fprintf(fp, "reset epoch = %d t = %.3f\n", 
				slow_epoch -1, run_time);
	fclose(fp);
}

static void update_sending_rate()
{
	if (latest_rtt >= min_rtt+T_THRESHOLD)
		s_gamma = (min_rtt*1.0+T_THRESHOLD)/latest_rtt;
	else if (latest_rtt > min_rtt + T_THRESHOLD*sigma)
		s_gamma = 1.0;
	else if (latest_rtt <= min_rtt+T_THRESHOLD*sigma)
		s_gamma = 1.0 + (min_rtt + T_THRESHOLD*sigma - latest_rtt)*1.0/min_rtt;

   	pthread_mutex_lock(&sendingCntLock);
		
	sending_rate = s_gamma*rcv_bytes*RESOLUTION/(M*DELTA);

	pthread_mutex_lock(&restartLock);
	rcv_bytes = 0;
    pthread_mutex_unlock(&restartLock);

    pthread_mutex_unlock(&sendingCntLock);
}

static void hystart_update(struct hybridss *hyss, uint32_t delay, int seq)
{

    if (!(hyss->found & hystart_detect)) {
        uint32_t now = pqsa_clock();
        /* */
		FILE *fp;
		double run_time = 0;
		struct timeval current_time;
		gettimeofday(&current_time,NULL);
		run_time = (current_time.tv_sec - start_time.tv_sec) + (current_time.tv_usec - start_time.tv_usec)/1000000.0;
		fp = fopen("einfo_slow_exit.txt", "a");	
        if ((now - hyss->last_ack) <= hystart_ack_delta) {
            hyss->last_ack = now;
		   	fprintf(fp, "ack found = %u t = %.3f ss_crt_win = %u now-start = %u slowstart_min = %u\n", 
						uint32_t(hyss->found), run_time, ss_crt_win, now - hyss->round_start, hyss->delay_min);
			//Divide by 16	
            if ((now - hyss->round_start) > hyss->delay_min >> 4){
				hyss->found |= HYSTART_ACK_TRAIN;
				fprintf(fp, "first way found = %u t = %.3f ss_crt_win = %u slowstart_min = %u now-start = %u\n", 
						uint32_t(hyss->found), run_time, ss_crt_win, hyss->delay_min, now - hyss->round_start);
			}
		}else{
        /* obtain the mininum delay of more than sampling packets */
			fprintf(fp, "delay found = %u t = %.3f ss_crt_win = %u cur_rtt = %u slowstart_min = %u Clamp = %lu pdu_seq=%d\n", 
					uint32_t(hyss->found), run_time, ss_crt_win, delay, hyss->delay_min, Clamp(hyss->delay_min >> 4), seq);
            if (delay > hyss->delay_min + Clamp(hyss->delay_min >> 4)){
				hyss->found |= HYSTART_DELAY;
				fprintf(fp, "first way found = %u\n", uint32_t(hyss->found));
			}
		
		}
		fclose(fp);
    }

    if (hyss->found & hystart_detect) {
        update_sending_rate();
        slow_start = false;
    }
}

static void displayError(const char *on_what)
{
    fputs(strerror(errno), stderr);
    fputs(":", stderr);
    fputs(on_what, stderr);
    fputs("\n", stderr);

    std::cout << "Error \n";

    exit(0);
}


static void restart_session()
{

    //pthread_mutex_lock(&restartLock);

    slow_start = true;
    pkt_seq = 0;
    sending_rate = 10.0;
    ss_id++;
    rcv_bytes = 0;
    s_gamma = 1.0;   //parameter

    hystart_reset(&s_hyss);

    //pthread_mutex_unlock(&restartLock);
}


/* initialization a pdu */
udp_packet_t *
udp_pdu_init(int seq, uint32_t packet_size, int ss_id)
{
    udp_packet_t *pdu;
    struct timeval time_stamp;

    if (packet_size <= sizeof(udp_packet_t)) {
        printf("defined packet size is smaller than headers. ");
        exit(0);
    }

    pdu = (udp_packet_t*)malloc(packet_size);

    if (pdu) {
        pdu->seq = seq;
        pdu->ss_id = ss_id;
        gettimeofday(&time_stamp, NULL);
        pdu->seconds = time_stamp.tv_sec;
        pdu->millis = time_stamp.tv_usec;
    }
    return pdu;
}

void* sending_thread(void *arg)
{
    int i, ret;
    int n_packets;
    udp_packet_t *pdu;
   	double sum_packets = 0.0;
	
    while (!terminate) {
        while (slow_start && crt_win > 0) {
            //first send one packet, then send two packets after receiving one ack 
			if (crt_win == 1){
				n_packets = crt_win;
			}else{
				n_packets = sender_pact;
			}
			pthread_mutex_lock(&sendingWinLock);
            crt_win = 0;
            pthread_mutex_unlock(&sendingWinLock);

            for (i = 0; i < n_packets; i++) {
                pkt_seq ++;
                pdu = udp_pdu_init(pkt_seq, MTU, ss_id);
				
				FILE *fp;
				double run_time = 0;
				struct timeval current_time;
				gettimeofday(&current_time,NULL);
				run_time = (current_time.tv_sec - start_time.tv_sec) + (current_time.tv_usec - start_time.tv_usec)/1000000.0;
				fp = fopen("einfo_slow_sendpact.txt", "a");	
				fprintf(fp, "t=%.4f pdu_seq=%d\n", run_time, int(pdu->seq));
				fclose(fp);

                ret = sendto(s, pdu, MTU, MSG_DONTWAIT, 
                        (struct sockaddr *)&adr_clnt, len_inet);
                if (ret < 0) {
                    /* if UDP buffer of OS is full, we exit slow start and 
                     * treat the current packet as lost */
                    if (errno == ENOBUFS || errno == EAGAIN || 
                            errno == EWOULDBLOCK) {
                        printf("errno\n");
                        if (slow_start) {
                            printf("exit slow start\n");
                            slow_start = false;
							
							FILE *fp;
							double run_time = 0;
							struct timeval current_time;
							gettimeofday(&current_time,NULL);
							run_time = (current_time.tv_sec - start_time.tv_sec) + (current_time.tv_usec - start_time.tv_usec)/1000000.0;
							fp = fopen("einfo_slow_exit.txt", "a");	
							fprintf(fp, "error t = %.3f min = %u cur = %u\n", 
									run_time, min_rtt, latest_rtt);
							fclose(fp);
							
                            pkt_seq --;
                            free(pdu);
                            break;
                        } else {
                            pkt_seq --;
                            free(pdu);
                            break;
                        }
                    } else
                        displayError("sendto(2)");
                }

                free(pdu);
            }
        }
		
		double tmp = 0.0;
        /* congestion control */
        while (!slow_start && sending_rate > 0.0) {
            int n_packets = 0;
			tmp = sending_rate*1.0*tau*DELTA/(1000*PACKET_SIZE);

			if (n_packets < 1)
           		sum_packets += tmp;

			if (sum_packets >= 1.0) {
				n_packets = (int)sum_packets;
				sum_packets -= n_packets;
			}
            pthread_mutex_lock(&sendingCntLock);
            sending_rate = 0.0;
            pthread_mutex_unlock(&sendingCntLock);

            for (i = 0; i < n_packets; i++) {
                pkt_seq++;
                pdu = udp_pdu_init(pkt_seq, MTU, ss_id);
                ret = sendto(s, pdu, MTU, MSG_DONTWAIT, 
                        (struct sockaddr *)&adr_clnt, len_inet);
                if (ret < 0) {
                    printf("error\n");
                    pkt_seq--;
                    free(pdu);
                }
            }
        }
    }
}



void timeout_handler(const boost::system::error_code& e)
{
    if (e) return;
    update_sending_rate();
    timer.expires_from_now (boost::posix_time::milliseconds(DELTA*tau));
    timer.async_wait(&timeout_handler);

    return;
}

void *timeout_thread(void *arg)
{
    boost::asio::io_service::work work(io);

    timer.expires_from_now (boost::posix_time::milliseconds(DELTA*M));
    timer.async_wait(&timeout_handler);
    io.run();

    return NULL;
}


void* receiver_thread(void *arg)
{
    double hyss_rate = 0.0;
    socklen_t len_inet;
    udp_packet_t *pdu;
    struct timeval received_time;
    static struct timeval last_time;

    len_inet = sizeof(struct sockaddr_in);
    pdu = (udp_packet_t *)malloc(sizeof(udp_packet_t));
    while (!terminate) {
        if (recvfrom(s, pdu, sizeof(udp_packet_t), 0, 
                    (struct sockaddr *)&adr_clnt, &len_inet) < 0)
            displayError("Receiver thread error");

        /* we have started a new ss session, this packet belongs to the old 
         * ss sesion, so we discard it. 
         * */
        if (pdu->ss_id < ss_id) {
            printf("old session\n");
            exit(-1);
        }
        
        gettimeofday(&received_time, NULL);
        pthread_mutex_lock(&restartLock);
		
		while (!t_win.empty() && (t_win.back() - t_win.front() > M*DELTA))
			t_win.pop();

		t_win.push(received_time.tv_sec*1000+received_time.tv_usec/1000);
        rcv_bytes = t_win.size()*PACKET_SIZE;

		pthread_mutex_unlock(&restartLock);

        cur_rtt = (received_time.tv_sec - pdu->seconds)*1000 + 
            (received_time.tv_usec - pdu->millis)/1000;

		if (latest_rtt == UINT_MAX)
			latest_rtt = cur_rtt;
		else
			latest_rtt = (1-alpha)*latest_rtt + alpha*cur_rtt;
       
        if (min_rtt > latest_rtt) {
            min_rtt = latest_rtt;
            s_hyss.delay_min = latest_rtt;
        }

        /* Receiving exactly the next sequence number, everything is ok no 
         * losses. */
        /*if (pdu->seq == seqLast + 1) {
            updateUnponReceiveringPacket(latest);
        } else if (pdu->seq < seqLast) {
        
        }*/
        /* ------------- pqsa slow start ------------- */
        /* Each ack received from client, we should update the 
         * hybrid slow start rate.
         * */
        if (slow_start) {
            ss_crt_win += 1;
            
			pthread_mutex_lock(&sendingWinLock);
            crt_win = ss_crt_win;
            pthread_mutex_unlock(&sendingWinLock);

            hystart_update(&s_hyss, latest_rtt, pdu->seq);
			if (slow_start){
				int index = int(pow(2, slow_epoch));
				if ((ss_crt_win % index) == 0){
					slow_epoch += 1;
					hystart_reset(&s_hyss);
				}
			}
        }

        //pthread_mutex_unlock(&restartLock);
    }
}

int main(int argc, char **argv)
{
    int i = 0, port;
    double relative_time = 0.0;
    double time_to_run;
    char dgram[512];
    char *name;

    udp_packet_t *pdu;
    struct sockaddr_in adr_inet;
    struct timeval current_time;
    struct timeval tempstamp;

    if (argc < 5) {
        std::cout << "syntax should be ./pqsa_server -p PORT -t TIME(sec) \n";
        exit(0);
    }

    /* parse parameters */
    while (i != (argc - 1)) {
        i = i + 1;
        if (!strcmp (argv[i], "-p")) {
            i = i + 1;
            port = atoi(argv[i]);
        } else if (!strcmp (argv[i], "-t")) {
            i = i + 1;
            time_to_run = std::stod(argv[i]);
        } else {
            std::cout << "syntax should be ./pqsa_server -p PORT -t TIME (sec)\n ";
            exit (0);
        }
    }
    
    /* initialization socket */
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == -1)
        displayError("socket error()");

    memset(&adr_inet, 0, sizeof adr_inet);
    adr_inet.sin_family = AF_INET;
    adr_inet.sin_port = htons(port);
    adr_inet.sin_addr.s_addr = INADDR_ANY;

    if (adr_inet.sin_addr.s_addr == INADDR_NONE)
        displayError("bad address.");

    len_inet = sizeof(struct sockaddr_in);

    if (bind (s, (struct sockaddr *)&adr_inet, sizeof(adr_inet)) < 0)
        displayError("bind()");

    std::cout << "Server " << port << " waiting for request\n";

    /* waiting for initialization packet */
    if (recvfrom(s, dgram, sizeof(dgram), 0, (struct sockaddr *)&adr_clnt, 
                &len_inet) < 0)
        displayError("recvfrom(2)");

    restart_session();
    gettimeofday(&start_time, NULL);
	
    /* starting the threads */
    if (pthread_create(&(timeout_tid), NULL, &timeout_thread, NULL) != 0)
        std::cout << "can't create thread: " << strerror(err) << "\n";
    if (pthread_create(&(sending_tid), NULL, &sending_thread, NULL) != 0)
        std::cout << "Can't create thread: " << strerror(err);
    if (pthread_create(&(receiver_tid), NULL, &receiver_thread, NULL) != 0)
        std::cout << "Can't create thread: " << strerror(err);

    std::cout << "Client " << port << " is connected\n";
    
    gettimeofday(&tempstamp, NULL);
	
    while (relative_time <= time_to_run) {
        gettimeofday(&current_time, NULL);
        relative_time = (current_time.tv_sec - start_time.tv_sec) + 
            (current_time.tv_usec - start_time.tv_usec)/1000000.0;
   	}
}
