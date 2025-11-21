# Makefile para cliente FTP 

CLTOBJ= MacasM-clienteFTP.o connectsock.o connectTCP.o passivesock.o passiveTCP.o errexit.o

all: MacasM-clienteFTP

MacasM-clienteFTP:	${CLTOBJ}
	cc -o MacasM-clienteFTP ${CLTOBJ}

clean:
	rm $(CLTOBJ) 
