import socket
import struct


def start_server():
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    host = '127.0.0.1'  # Change this to your server IP if needed
    port = 12341       # Choose any available port number

    server_socket.bind((host, port))
    server_socket.listen(1)
    print("Server listening on {}:{}".format(host, port))
    while True:
        conn, addr = server_socket.accept()
        print("Connected to", addr)

        while True:
            data = conn.recv(4)  # Receive 4 bytes (size of int)
            if len(data) != 4:
                break
            received_data = struct.unpack('i', data)[0]  # Unpack received binary data to an integer
            print("Received integer from client:", received_data)
            num = int(input("server : "))
            conn.sendall(struct.pack('i', num))

        print("Connection closed with", addr)
        conn.close()
if __name__ == "__main__":
    start_server()