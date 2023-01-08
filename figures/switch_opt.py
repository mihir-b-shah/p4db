
import pandas as pd
import sys


def ycsb(df):
    '''
    TABLE_SIZE = 10M
    YCSB_HOT_SIZE = 50 keys
    YCSB_HOT_PROB = 75%
        - since all txns are pure hot/cold, we can see NUM_TXNS*YCSB_HOT_PROB is # of txns on switch.
    YCSB_REMOTE_PROB = 20%
    WORKLOAD = YCSB
    NUM_NODES = 4
    NUM_TXNS = 500000
    CSV_FILE_* = <see in data/>

    virtually no aborts.
    almost linear scaling with switch_no_conflict, less otherwise
    '''
    df = df[df['workload'] == 'ycsb']
    df = df[df['ycsb_write_prob'] == 50]
    df = df[df['lm_on_switch'] == False]
    df = df[df['cc_scheme'] == 'no_wait']
    df = df[df['use_switch'] == True]
    df = df[df['run'] == 1]
    #df = df[df['node_id'] == 0]
    return df

'''
Unoptimized: opti=False, snc=False, slow.csv
Fast-Recirc: opti=False, snc=False, fast.csv
Fine-locking, opti=True, snc=False, fast.csv
Declustered, opti=False, snc=True, fast.csv
'''

def fmt_df(df):
    df['est_txn_latency_us'] = df['num_txn_workers'] * df['avg_duration'] / df['total_commits']
    return df[['total_cps', 'est_txn_latency_us']]

def unopt(df_slow):
    df_slow = df_slow[df_slow['ycsb_opti_test'] == False]
    df_slow = df_slow[df_slow['switch_no_conflict'] == False]
    return fmt_df(df_slow)

def fast_recirc(df):
    df = df[df['ycsb_opti_test'] == False]
    df = df[df['switch_no_conflict'] == False]
    return fmt_df(df)

def fine_lock(df):
    df = df[df['ycsb_opti_test'] == True]
    df = df[df['switch_no_conflict'] == False]
    return fmt_df(df)

def declustered(df):
    df = df[df['ycsb_opti_test'] == False]
    df = df[df['switch_no_conflict'] == True]
    return fmt_df(df)
    
df_slow = pd.read_csv('data/exp_ycsb_optis_slow.csv')
df_slow = ycsb(df_slow)
df = pd.read_csv('data/exp_ycsb_optis.csv')
df = ycsb(df)

print('Unoptimized')
print(unopt(df_slow))
print('Fast-Recirc')
print(fast_recirc(df))
print('Fine-Lock')
print(fine_lock(df))
print('Declustered')
print(declustered(df))
