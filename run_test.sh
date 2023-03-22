export LD_LIBRARY_PATH='/lusr/opt/gcc-11.1.0/lib64/:/u/mihirs/.local/usr/local/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH'
LD_PRELOAD='/lusr/opt/gcc-11.1.0/lib64/libasan.so /lusr/opt/gcc-11.1.0/lib64/libubsan.so'
set -e
ninja -C build/
export ASAN_OPTIONS='detect_leaks=0'
if [[ "$2" == "DBG" ]]
then
	gdb --args ./build/p4db --tenant_id 0 --node_id $1 --num_nodes 2 --num_txn_workers 3 --use_switch=true --num_txns 1000000 --write_prob 100 --table_size 10000000 --trace_fname="generator/node$1_S2_z99_N1000000_n8_k10000000_txns.csv" --dist_fname="generator/dist_S2_z99_N1000000_n8_k10000000.txt"
else
	./build/p4db --tenant_id 0 --node_id $1 --num_nodes 2 --num_txn_workers 3 --use_switch=true --num_txns 1000000 --write_prob 100 --table_size 10000000 --trace_fname="generator/node$1_S2_z99_N1000000_n8_k10000000_txns.csv" --dist_fname="generator/dist_S2_z99_N1000000_n8_k10000000.txt" 2>logs/log_$1
fi
#gdb --args ./build/p4db --node_id $1 --num_nodes 2 --num_txn_workers 1 --use_switch=false --num_txns 500000 --write_prob 100 --hot_size 1000 --table_size 10000000 --trace_fname="generator/node$1_z80_N100000_n16_k10000000_h100_c0_txns.csv" --dist_fname="generator/dist_z80_N100000_n16_k10000000_h100_c0.txt"
#./build/p4db --node_id $1 --num_nodes 1 --num_txn_workers 8 --workload ycsb --use_switch=false --num_txns 500000 --tpcc_num_warehouses 1 --tpcc_new_order_remote_prob 0 --tpcc_payment_remote_prob 0
