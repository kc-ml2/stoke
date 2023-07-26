import socket
import random 


class RandomAgent:
    def act(self):
        return random.randint(0, 2)

class StokeEnv:
    def __init__(self):
        pass
        
    def reset(self): 
        pass
    
    def _recv_all(self):
        ret = ""
        while True:
            response = self.sock.recv(1024).decode()
            if not response:
                break
            ret += response
        
        return ret

    def step(self, action):
        print("***ACTION***")
        print(action)
        
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect(('localhost', 8080))
        
        self.sock.sendall((str(action) + "\n").encode())
        ret = self._recv_all()
        self.sock.close()

        print("***OBSERVATON*** ")
        print(ret)

        return ret

    def close(self):
        self.sock.close()

if __name__ == "__main__":
    # docker.exec_run("stoke_server start")

    env = StokeEnv()
    env.reset()

    agent = RandomAgent()

    for i in range(10):
        action = agent.act()
        env.step(action)

    env.close()