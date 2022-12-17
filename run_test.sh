export LD_LIBRARY_PATH=/lusr/opt/gcc-11.1.0/lib64/ 
./build/p4db --node_id $1 --num_nodes 2 --num_txn_workers 8 --workload ycsb --use_switch=false --num_txns 500000 --ycsb_table_size 1000 --ycsb_write_prob 50 --ycsb_remote_prob 0 --ycsb_hot_prob 75 --ycsb_hot_size 50
# use_switch=False
