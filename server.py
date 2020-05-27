import socket

HOST = '127.0.0.1'  # Standard loopback interface address (localhost)
PORT = 60001        # Port to listen on (non-privileged ports are > 1023)

def calcul(value):
    if (int(value)<10): 
        return 50
    if (int(value)>=10): 
        return 50
        
def testData(dataToTest):
    if dataToTest.count("@")==2 :
        listData=dataToTest.split(':')
        rep = listData[-4]+':'+listData[-3]+':'+listData[-2]+"\n"  
        conn.sendall(rep.encode())
        return 1
    else: 
        return 0
        
while(True):

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
     s.bind((HOST, PORT))
     s.listen(1)
     conn, addr = s.accept()
     
    with conn:
        buf =""
        print('Connected by', addr)
        while True:
            data = conn.recv(1024)
            if data:
                buf += data.decode("utf-8")
                reinit = testData(buf)
                if (reinit==1):
                    buf=""
            else:
                break
