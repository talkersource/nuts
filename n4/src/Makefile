##############################################################################
# FILE: Makefile
# LVU : 1.3.7
#
# DESC:
# NUTS-IV makefile. Uncomment the appropriate CPP etc lines for your OS.
#
##############################################################################

CPP="C++ Compiler not set in makefile"
HEADERS=globals.h version.h
ALL_HEADERS=globals.h templates.h version.h

# Solaris.
# The filter removes bloody irritating warnings about string literals
# being used in array initialisations and passed into variadic functions with
# a char* argument and also about the pthread_create() function not being 
# passed an extern "C" function pointer though the compiler ignores appropriate
# extern "C" statements that should rectify it! The cat command is because
# egrep will return 1 if it finds the string and this would otherwise cause
# the make to fail, cat will return 0.
#CPP=CC -DSOLARIS 
#LIBS=-lsocket -lnsl -lpthread
#FILTER=2>&1 | egrep -v "literal converted|pthread_create" | cat

# Linux
CPP=c++ -Wall -pedantic -DLINUX 
LIBS=-pthread -lcrypt

# FreeBSD
#CPP=c++ -Wall -pedantic -DBSD -D_THREAD_SAFE -g 
#LIBS=-pthread -lcrypt

# OS/X
#CPP=c++ -Wall -pedantic -DBSD -D_THREAD_SAFE -g 
#LIBS=-lpthread 

# AIX 
#CPP=xlC_r 

# Anything else try the above or these
#CPP=cpp
#CPP=cc


nuts4: main.o events.o cl_user1.o cl_user2.o cl_local_user.o cl_remote_user.o \
	cl_friend.o cl_server.o cl_group.o cl_splitline.o cl_editor.o \
	cl_mail.o cl_board.o cl_msginfo.o misc.o threads.o
	$(CPP) *.o $(LIBS) -o nuts4

main.o: main.cc $(HEADERS)
	$(CPP) -c main.cc $(FILTER)

events.o: events.cc $(HEADERS)
	$(CPP) -c events.cc $(FILTER)

misc.o: misc.cc $(HEADERS)
	$(CPP) -c misc.cc $(FILTER)

threads.o: threads.cc $(HEADERS)
	$(CPP) -c threads.cc $(FILTER)

cl_user1.o: cl_user1.cc $(ALL_HEADERS)
	$(CPP) -c cl_user1.cc $(FILTER)

cl_user2.o: cl_user2.cc $(ALL_HEADERS)
	$(CPP) -c cl_user2.cc $(FILTER)

cl_local_user.o: cl_local_user.cc $(ALL_HEADERS)
	$(CPP) -c cl_local_user.cc $(FILTER)

cl_remote_user.o: cl_remote_user.cc $(ALL_HEADERS)
	$(CPP) -c cl_remote_user.cc $(FILTER)

cl_friend.o: cl_friend.cc $(ALL_HEADERS)
	$(CPP) -c cl_friend.cc $(FILTER)

cl_server.o: cl_server.cc $(ALL_HEADERS)
	$(CPP) -c cl_server.cc $(FILTER)

cl_group.o: cl_group.cc $(ALL_HEADERS)
	$(CPP) -c cl_group.cc $(FILTER)

cl_splitline.o: cl_splitline.cc $(HEADERS)
	$(CPP) -c cl_splitline.cc $(FILTER)

cl_editor.o: cl_editor.cc $(HEADERS)
	$(CPP) -c cl_editor.cc $(FILTER)

cl_mail.o: cl_mail.cc $(HEADERS)
	$(CPP) -c cl_mail.cc $(FILTER)

cl_board.o: cl_board.cc $(HEADERS)
	$(CPP) -c cl_board.cc $(FILTER)

cl_msginfo.o: cl_msginfo.cc $(HEADERS)
	$(CPP) -c cl_msginfo.cc $(FILTER)

clean:
	rm -rf nuts4 SunWS_cache Templates.DB ir.out core ../core *.o 2> /dev/null
