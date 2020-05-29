import socket

HOST = '127.0.0.1'  # Standard loopback interface address (localhost)
PORT = 60001        # Port to listen on (non-privileged ports are > 1023)

#This function check the flow of the border node and take data if there's any
def testData(dataToTest):
    if dataToTest.count("@")==2 :
        listData=dataToTest.split(':')
        return listData[-4],listData[-3],listData[-2]#we take the source address and the data of the msg
    else: 
        return [-1]*3 #we don't have to add data to memory data
        
def leastSquare(yvalue,idnode):

    xvalue = list(range(0,30))
    sumx=sumx2=sumy=sumyx=0

    for i in range(0,30):
        sumx=sumx+xvalue[i]
        sumx2=sumx2+xvalue[i]*xvalue[i]
        sumy=sumy+yvalue[i]
        sumyx=sumyx+yvalue[i]*xvalue[i]

    #//to find the a of y=ax+b
    a=(30*sumyx-sumx*sumy)/(30*sumx2-sumx*sumx)
    
    if a>1: 
        conn.sendall((str(idnode)+'\n').encode())#when threshold is reached, open valve with id=idnode

while(True):

    #AF_INET for ipv4 and SOCK_STREAM for tcp
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
     s.bind((HOST, PORT))
     s.listen(1)
     conn, addr = s.accept()
     
    with conn:
        #we stock the data for each node in memorydata
        #the index corresponding to a node id is determined in whichindex
        #nbrdata is the number of times we added a value for each node id (to verify there is <=30 datas)
        memorydata = []
        whichindex=[]
        nbrdata = []
 
        buf =""
        print('Connected by', addr)

        while True:
            
            data = conn.recv(1024)
            if data:
                buf += data.decode("utf-8") #we scan all the data from the printf of contiki
                dataTested = testData(buf)
                if (int(dataTested[0])!=-1): #data to add to memorydata
                    buf=""#remove all data scanned

                    if (int(dataTested[0]) not in whichindex):#if it's the first data for this node id
                        whichindex.insert(len(whichindex),int(dataTested[0]))
                        nbrdata.insert(len(nbrdata),0)#add a counting list nbrdata for this id node
                        memorydata.append([])#add a list of data for this id node 

                    indexid = whichindex.index(int(dataTested[0]))#the index of the id in memorydata
                    nbrdata [indexid] +=1 #one data more (max 30)
                    memorydata[indexid].insert(len(memorydata[indexid]),int(dataTested[2])) 
                    if (nbrdata [indexid]==31):#the limit is broken we have to remove the oldest data     
                        nbrdata [indexid] -=1
                        memorydata[indexid].pop(0) #remove the oldest      
                        leastSquare(memorydata[indexid],int(dataTested[0]))#compute the value of leastsquare
                    elif(nbrdata [indexid]==30):
                        leastSquare(memorydata[indexid],int(dataTested[0]))#compute the value of leastsquare               
                 
            else:
                break
