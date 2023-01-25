
import socket
import threading

ip_list = []
ip_list_cv = threading.Condition()
N = 1
n = 0

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(('0.0.0.0', 5001))
sock.listen(1)

def conn_handler(csock, addr):
    global n
    data = csock.recv(1024)
    ip_list_cv.acquire()
    ip_list.append(str(addr[0]))
    n += 1

    if n < N:
        ip_list_cv.wait()
    else:
        ip_list_cv.notify_all()
    ip_list_cv.release()

    assert(n == N)
    print('Send', ip_list)
    csock.send(' '.join(ip_list).encode())
    csock.close()
    
try:
    thrs = []
    while True:
        new_sock, addr = sock.accept()
        thr = threading.Thread(target=conn_handler, args=(new_sock, addr))
        thr.start()
        thrs.append(thr)
    for thr in thrs:
        thr.join()
finally:
    sock.close()
