CC += -std=c++11
CFLGAS += -O0 -g -Wall -Wno-error 
LDLIBS += -lpthread -lm -lstdc++ -lboost_system
DEPS = pqsa.hpp
OBJ = pqsa_server.cpp

%.o: %.c $(DEPS)
		$(CC) -c -o $@ $< $(CFLAGS) $(LDLIBS)

pqsa_server: $(OBJ)
		$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)

clean:
		rm -f main *.o 
