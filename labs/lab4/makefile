main:
	gcc -o preflow preflow.c pthread_barrier.c -g -O3 -pthread
	time sh check-solution.sh ./preflow
	@echo PASS all tests

seq:
	gcc -o sequential sequential.c -g -O3
	time sh check-solution.sh ./sequential
	@echo PASS all tests
