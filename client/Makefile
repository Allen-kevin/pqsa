CC += -std=c++11
CFLGAS = -g
LDLIBS += -lpthread -lstdc++
DEPS = pqsa.hpp
OBJ = pqsa_client.cpp

%.o: %.c $(DEPS)
		$(CC) -c -o $@ $< $(CFLAGS) $(LDLIBS)

pqsa_client: $(OBJ)
		$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)

clean:
		rm -f main *.o 
