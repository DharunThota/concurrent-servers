# Threaded socket server - accepting multiple clients concurrently, by
# dispatching them into a thread pool.
#https://docs.python.org/3/library/socket.html

import argparse
from concurrent.futures import ThreadPoolExecutor
from enum import Enum
import socket
import sys

ProcessingState = Enum('ProcessingState', 'WAIT_FOR_MSG IN_MSG')

def serve_connection(sockobj, client_address):
    print('{0} connected'.format(client_address))
    #Send data to the socket, continues to send data from bytes until either all data has been sent or an error occurs. 
    # None is returned on success.
    sockobj.sendall(b'*')
    state = ProcessingState.WAIT_FOR_MSG

    while True:
        try:
            buf = sockobj.recv(1024)
            if not buf:
                break
        except IOError as e:
            break
        for b in buf:
            if state == ProcessingState.WAIT_FOR_MSG:
                if b == ord(b'^'):
                    state = ProcessingState.IN_MSG
            elif state == ProcessingState.IN_MSG:
                if b == ord(b'$'):
                    state = ProcessingState.WAIT_FOR_MSG
                else:
                    sockobj.send(bytes([b+1]))
            else:
                assert False
    
    print('{0} done'.format(client_address))
    #https://stackoverflow.com/a/10019605/20051006
    sys.stdout.flush()
    sockobj.close()

if __name__ == '__main__':
    argparser = argparse.ArgumentParser('Threadpool server')
    argparser.add_argument('--port', type=int, default=9090, help='Server Port')
    argparser.add_argument('-n', type=int, default=64, help='Number of threads in pool')
    args = argparser.parse_args()

    pool = ThreadPoolExecutor(args.n)
    sockobj = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sockobj.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sockobj.bind(('localhost', args.port))
    sockobj.listen(15)

    try:
        while True:
            client_socket, client_address = sockobj.accept()
            pool.submit(serve_connection, client_socket, client_address)
    except KeyboardInterrupt as e:
        print(e)
        sockobj.close()