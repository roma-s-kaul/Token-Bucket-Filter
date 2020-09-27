tokenBucketFilter: tokenBucketFilter.o list.o
	gcc -o tokenBucketFilter -g tokenBucketFilter.o list.o -lm -lpthread

tokenBucketFilter.o: tokenBucketFilter.c list.h
	gcc -g -c -Wall tokenBucketFilter.c -D_POSIX_PTHREAD_SEMANTICS

list.o: list.c list.h
	gcc -g -c -Wall list.c

clean:
	rm -f *.o tokenBucketFilter