#export LD_LIBRARY_PATH='/lusr/opt/gcc-11.1.0/lib64/:/u/mihirs/.local/usr/local/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH'

if [[ "$1" == "DBG" ]]
then
	gdb --args ./build/p4db --node_id 0 --num_nodes 1 --num_txn_workers 2 --use_switch=true --num_txns 500000 --write_prob 100 --table_size 10000000 --trace_fname="generator/node0_z99_N1000000_n8_k10000000_h100_c0_txns.csv" --dist_fname="generator/dist_z99_N1000000_n8_k10000000_h100_c0.txt"
else
	./build/p4db --node_id 0 --num_nodes 1 --num_txn_workers 2 --use_switch=true --num_txns 500000 --write_prob 100 --table_size 10000000 --trace_fname="generator/node0_z99_N1000000_n8_k10000000_h100_c0_txns.csv" --dist_fname="generator/dist_z99_N1000000_n8_k10000000_h100_c0.txt"
fi

#gdb --args ./build/p4db --node_id $1 --num_nodes 2 --num_txn_workers 1 --use_switch=false --num_txns 500000 --write_prob 100 --hot_size 1000 --table_size 10000000 --trace_fname="generator/node$1_z80_N100000_n16_k10000000_h100_c0_txns.csv" --dist_fname="generator/dist_z80_N100000_n16_k10000000_h100_c0.txt"
#./build/p4db --node_id $1 --num_nodes 1 --num_txn_workers 8 --workload ycsb --use_switch=false --num_txns 500000 --tpcc_num_warehouses 1 --tpcc_new_order_remote_prob 0 --tpcc_payment_remote_prob 0
