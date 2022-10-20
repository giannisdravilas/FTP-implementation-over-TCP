ALL: dataServer remoteClient

dataServer: dataServer.o
	g++ dataServer.o -o dataServer -lpthread

remoteClient: remoteClient.o
	g++ remoteClient.o -o remoteClient -lpthread

run: dataServer
	./dataServer -p 5180 -s 2 -q 3 -b 4

clean:
	rm -f dataServer dataServer.o remoteClient remoteClient.o