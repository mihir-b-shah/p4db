#!/lusr/bin/python3.10

import pandas as pd
import numpy as np

def describe(df, qs):
    print(df.describe(percentiles=qs))

def order_sizes(df):
    df = df.groupby(by=['order_id']).count()
    df = df.rename(columns={'product_id':'order_size'})
    return df

def hot_dist(df):
    df = df.groupby(by=['product_id']).count() 
    df = df.rename(columns={'order_id':'item_freq'})
    df = df.sort_values(by=['item_freq'], ascending=False)
    return df

def hot_order_sizes(df, frac_hot):
    hot = hot_dist(df) 
    hot = hot.head(int(len(hot)*frac_hot))
    hot = hot.drop(columns=['item_freq'])
    hot_txns = df.join(hot, on='product_id', how='inner')
    ret = order_sizes(hot_txns)
    ret = ret.rename(columns={'order_size':'hot_part_order_size'})
    return ret

df = pd.read_csv('orders.csv')

print(describe(hot_dist(df), 1-np.logspace(-4,0,num=21)[::-1]))
print(describe(order_sizes(df), np.linspace(0,1,num=11)))
print(describe(hot_order_sizes(df, 0.01), np.linspace(0,1,num=11)))
