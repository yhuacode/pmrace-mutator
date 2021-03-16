CFLAGS += -Wall -O3
AFL_FUZZ_BIN = /home/vagrant/AFLplusplus/afl-fuzz
AFL_CUSTOM_MUTATOR_DIR = /home/vagrant/pmrace-mutator
AFL_INITIAL_SEEDS_DIR = $(AFL_CUSTOM_MUTATOR_DIR)/corpus
AFL_OUTPUT_DIR = $(AFL_CUSTOM_MUTATOR_DIR)/output
PM_POOL_DIR = $(AFL_CUSTOM_MUTATOR_DIR)/pools
PM_WORKLOADS_DIR = /home/vagrant/pm-workloads-afl-build

%-mutator.so: %-mainlist.o %-mutator.o
	gcc -fPIC -shared -g -o $@ $^ $(CFLAGS)

dump-%: %-mainlist.o dump.c
	gcc -g -o $@ -I../AFLplusplus/include $(CFLAGS) $^

validate-%: validate-crash.sh dump-%
	@bash $< $* 4

clht-%.o: %.c
	gcc -fPIC -g -o $@ -c $< -I../AFLplusplus/include $(CFLAGS) -DCLHT

memcached-%.o: %.c
	gcc -fPIC -g -o $@ -c $< -I../AFLplusplus/include $(CFLAGS) -DMEMCACHED

cceh-%.o: %.c
	gcc -fPIC -g -o $@ -c $< -I../AFLplusplus/include $(CFLAGS) -DCCEH

ff-%.o: %.c
	gcc -fPIC -g -o $@ -c $< -I../AFLplusplus/include $(CFLAGS) -DFAST_FAIR

clevel-%.o : %.c
	gcc -fPIC -g -o $@ -c $< -I../AFLplusplus/include $(CFLAGS) -DCLEVEL

test-memcached: memcached-mutator.so
	-@kill -9 $(shell lsof -t -i:11211)
	-@rm -rf $(AFL_OUTPUT_DIR)/out_memcached
ifeq ($(USE_AFL), 1)
	@$(AFL_FUZZ_BIN) -m 1024 -i $(AFL_INITIAL_SEEDS_DIR)/memcached -o $(AFL_OUTPUT_DIR)/out_memcached -- $(PM_WORKLOADS_DIR)/memcached-pmem/memcached -A -o pslab_force,pslab_file=$(PM_POOL_DIR)/mem_pool
else
	@AFL_CUSTOM_MUTATOR_LIBRARY=$(AFL_CUSTOM_MUTATOR_DIR)/$< $(AFL_FUZZ_BIN) -m 1024 -i $(AFL_INITIAL_SEEDS_DIR)/memcached -o $(AFL_OUTPUT_DIR)/out_memcached -- $(PM_WORKLOADS_DIR)/memcached-pmem/memcached -A -o pslab_force,pslab_file=$(PM_POOL_DIR)/mem_pool
endif

test-clht: clht-mutator.so
	-@rm -rf $(AFL_OUTPUT_DIR)/out_clht
ifeq ($(USE_AFL), 1)
	@PMEM_POOL=$(PM_POOL_DIR)/clht_pool $(AFL_FUZZ_BIN) -t 10000 -m 1024 -i $(AFL_INITIAL_SEEDS_DIR)/clht -o $(AFL_OUTPUT_DIR)/out_clht -- $(PM_WORKLOADS_DIR)/RECIPE/build/ycsb clht 4
else
	@PMEM_POOL=$(PM_POOL_DIR)/clht_pool AFL_CUSTOM_MUTATOR_LIBRARY=$(AFL_CUSTOM_MUTATOR_DIR)/$< $(AFL_FUZZ_BIN) -t 10000 -m 1024 -i $(AFL_INITIAL_SEEDS_DIR)/clht -o $(AFL_OUTPUT_DIR)/out_clht -- $(PM_WORKLOADS_DIR)/RECIPE/build/ycsb clht 4
endif

test-cceh: cceh-mutator.so
	-@rm -rf $(AFL_OUTPUT_DIR)/out_cceh
ifeq ($(USE_AFL), 1)
	@$(AFL_FUZZ_BIN) -t 20000 -m 1024 -i $(AFL_INITIAL_SEEDS_DIR)/cceh -o $(AFL_OUTPUT_DIR)/out_cceh -- $(PM_WORKLOADS_DIR)/CCEH/CCEH-PMDK/bin/cceh $(PM_POOL_DIR)/cceh_pool 4
else
	@AFL_CUSTOM_MUTATOR_LIBRARY=$(AFL_CUSTOM_MUTATOR_DIR)/$< $(AFL_FUZZ_BIN) -t 20000 -m 1024 -i $(AFL_INITIAL_SEEDS_DIR)/cceh -o $(AFL_OUTPUT_DIR)/out_cceh -- $(PM_WORKLOADS_DIR)/CCEH/CCEH-PMDK/bin/cceh $(PM_POOL_DIR)/cceh_pool 4
endif

test-ff: ff-mutator.so
	-@rm -rf $(AFL_OUTPUT_DIR)/out_ff
ifeq ($(USE_AFL), 1)
	@$(AFL_FUZZ_BIN) -t 10000 -m 1024 -i $(AFL_INITIAL_SEEDS_DIR)/ff -o $(AFL_OUTPUT_DIR)/out_ff -- $(PM_WORKLOADS_DIR)/FAST_FAIR/concurrent_pmdk/btree_concurrent -p $(PM_POOL_DIR)/ff_pool -t 4
else
	@AFL_CUSTOM_MUTATOR_LIBRARY=$(AFL_CUSTOM_MUTATOR_DIR)/$< $(AFL_FUZZ_BIN) -t 10000 -m 1024 -i $(AFL_INITIAL_SEEDS_DIR)/ff -o $(AFL_OUTPUT_DIR)/out_ff -- $(PM_WORKLOADS_DIR)/FAST_FAIR/concurrent_pmdk/btree_concurrent -p $(PM_POOL_DIR)/ff_pool -t 4
endif

test-clevel: clevel-mutator.so
	-@rm -rf $(AFL_OUTPUT_DIR)/out_clevel
ifeq ($(USE_AFL), 1)
	@$(AFL_FUZZ_BIN) -t 10000 -m 1024 -i $(AFL_INITIAL_SEEDS_DIR)/clevel -o $(AFL_OUTPUT_DIR)/out_clevel -- $(PM_WORKLOADS_DIR)/Clevel-Hashing/build/tests/clevel_hash_ycsb $(PM_POOL_DIR)/clevel_pool 4
else
	@AFL_CUSTOM_MUTATOR_LIBRARY=$(AFL_CUSTOM_MUTATOR_DIR)/$< $(AFL_FUZZ_BIN) -t 10000 -m 1024 -i $(AFL_INITIAL_SEEDS_DIR)/clevel -o $(AFL_OUTPUT_DIR)/out_clevel -- $(PM_WORKLOADS_DIR)/Clevel-Hashing/build/tests/clevel_hash_ycsb $(PM_POOL_DIR)/clevel_pool 4
endif
