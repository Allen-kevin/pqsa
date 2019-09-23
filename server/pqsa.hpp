#ifndef PQSA_H_
#define PQSA_H_

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>
#include <iostream>
#include <vector>
#include <math.h>
#include <atomic>
#include <fstream>
#include <queue>
#include <map>
#include <atomic>
// ALGLIB library
//#include "../lib/alglib/src/ap.h"
//#include "../lib/alglib/src/interpolation.h"

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/assign/std/vector.hpp>

#define  SS_INIT_TIMEOUT 1000.0

// VERUS PARAMETERS
#define  EPOCH 5e3 // Verus epoch in microseconds
#define	 SS_EXIT_THRESHOLD 500.0
#define  MAX_TIMEOUT 1000.0
#define  MIN_TIMEOUT 150.0
#define  MISSING_PKT_EXPIRY 150.0
#define  MAX_W_DELAY_CURVE 40000

//using namespace alglib;

//pthread_mutex_t restartLock;

typedef struct __attribute__((packed, aligned(2))) m {
    int ss_id;
    unsigned long long seq;
    long long w;
    long long seconds;
    long long millis;
} udp_packet_t;


struct hybridss {
    uint32_t delay_min;
    uint32_t round_start;
    uint32_t last_ack;
    uint32_t curr_rtt;
    uint8_t  found;
    uint8_t sample_cnt;
};

#endif /* PQSA_H_ */
