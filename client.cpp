#include "common.h"
#include "FIFOreqchannel.h"
#include "TCPRequestChannel.h"
#include "BoundedBuffer.h"
#include "HistogramCollection.h"
#include <sys/wait.h>
#include <thread>
using namespace std;

template <class T>
pair<int, T> readPacket(vector<char> packet, T) {
    int* patient = (int*)packet.data();
    T* val = (T*)(packet.data() + sizeof(int));
    return make_pair(*patient, *val);
}

template <class T>
vector<char> makePacket(int patient, T val) {
    char request[sizeof(int) + sizeof(T)];
    memcpy(request, &patient, sizeof(int));
    memcpy(request + sizeof(int), &val, sizeof(T));
    return vector<char>(request, request + sizeof(request));
}

void patient_thread_function(BoundedBuffer& rbb, int n, int p) {
    for(int currN = 0; currN < n; currN++) {
        rbb.push(makePacket(p, DataRequest(p, 0.0 + 0.004 * currN, 1)));
    }
}

void patient_file_thread_function(BoundedBuffer& rbb, int m, int p, string f, long fileLen) {
    FileRequest fm(0, 0);
    int len = sizeof(FileRequest) + f.size() + 1;
    char buf2[len];
    memcpy(buf2, &fm, sizeof(FileRequest));
    strcpy(buf2 + sizeof(FileRequest), f.c_str());

    long remaining = fileLen;
    FileRequest* fq = (FileRequest*)buf2;

    while(remaining > 0) {
        fq->length = min(remaining, (long)m);
        rbb.push(vector<char>(buf2, buf2 + sizeof(buf2)));
        remaining -= fq->length;
        fq->offset += fq->length;
    }
}

void worker_thread_function(BoundedBuffer& rbb, BoundedBuffer& hbb, TCPRequestChannel* channel) {
    while(true) {
        vector<char> rq = rbb.pop();
        if(strcmp(rq.data(), "quit") == 0) {
            break;
        }
        pair<int, DataRequest> packetData = readPacket(rq, DataRequest(0, 0, 0)); //Dummy DataRequest for readPacket template purposes
        DataRequest drq = packetData.second;
        channel->cwrite(&drq, sizeof(DataRequest));
        double reply;
        channel->cread(&reply, sizeof(double));
        hbb.push(makePacket(packetData.first, reply));
    }
}

void worker_file_thread_function(BoundedBuffer& rbb, int m, string f, TCPRequestChannel* channel) {
    while(true) {
        vector<char> rq = rbb.pop();
        if(strcmp(rq.data(), "quit") == 0) {
            break;
        }
        FileRequest* fq = (FileRequest*)rq.data();
        int len = sizeof(FileRequest) + f.size() + 1;

        char fileBuf[m];
        channel->cwrite(rq.data(), len);
        channel->cread(fileBuf, m);
        ofstream ofs("received/" + f, std::ios::binary | std::ios::ate | std::ios::in);
        ofs.seekp(fq->offset);
        ofs.write(fileBuf, fq->length);
        ofs.close();
    }
}

void histogram_thread_function (BoundedBuffer& hbb, vector<Histogram*>& histograms, vector<mutex>& histogramLocks) {
    while(true) {
        vector<char> rq = hbb.pop();
        if(strcmp(rq.data(), "quit") == 0) {
            break;
        }
        pair<int, double> packetData = readPacket(rq, 0.0); //Dummy double value for readPacket template purposes
        lock_guard<mutex> lg(histogramLocks.at(packetData.first - 1));
        histograms.at(packetData.first - 1)->update(packetData.second);
    }
}

int main(int argc, char *argv[]){
	int opt;
	int n = 1;
    int p = 1;
    int h = 1;
    int w = 1;
    int b = 10; // size of bounded buffer, note: this is different from another variable buffercapacity
    string f;
    int m = MAX_MESSAGE;
    string host = "127.0.0.1";
    string port = "25566";

	// take all the arguments first because some of these may go to the server
	while ((opt = getopt(argc, argv, "n:p:z:w:b:f:m:h:r:")) != -1) {
		switch(opt) {
			case 'n':
				n = stoi(optarg);
				break;
            case 'p':
                p = stoi(optarg);
                break;
            case 'z':
                h = stoi(optarg);
                break;
            case 'w':
                w = stoi(optarg);
                break;
            case 'b':
                b = stoi(optarg);
                break;
            case 'f':
                f = optarg;
                break;
            case 'm':
                m = stoi(optarg);
                break;
            case 'h':
                host = optarg;
                break;
            case 'r':
                port = optarg;
		}
	}
    /*
    int pid = fork ();
    if (pid < 0){
        EXITONERROR ("Could not create a child process for running the server");
    }
    if (!pid){ // The server runs in the child process
        char* args[] = {"./server", "-m", &to_string(m)[0], "-r", &port[0], nullptr};
        if (execvp(args[0], args) < 0){
            EXITONERROR ("Could not launch the server");
        }
        return 0;
    }
    */
	BoundedBuffer request_buffer(b);
    BoundedBuffer histogram_buffer(b);
	HistogramCollection hc;

	struct timeval start, end;
    gettimeofday (&start, 0);

    /* Start all threads here */
    vector<thread> patientThreads;
    vector<thread> workerThreads;
    vector<thread> histogramThreads;
    vector<TCPRequestChannel*> channels;
    vector<Histogram*> histograms; //The HistrogramCollection destructor deletes these
    vector<mutex> histogramLocks(p);
    for(int i = 0; i < p; i++) {
        histograms.push_back(new Histogram(10, -2.0, 2.0));
        hc.add(histograms.back());
    }

    char quitMsg[] = "quit";
    vector<char> quitPacket(quitMsg, quitMsg + sizeof(quitMsg));

    if(!f.empty()) {
        TCPRequestChannel chan(host, port);
        FileRequest fm(0, 0);
        int len = sizeof(FileRequest) + f.size() + 1;
        char buf2[len];
        memcpy(buf2, &fm, sizeof(FileRequest));
        strcpy(buf2 + sizeof(FileRequest), f.c_str());
        chan.cwrite(buf2, len);
        int64 filelen;
        chan.cread(&filelen, sizeof(int64));
        if (isValidResponse(&filelen)) {
            cout << "File length is: " << filelen << " bytes" << endl;
        }
        Request q(QUIT_REQ_TYPE);
        chan.cwrite(&q, sizeof(Request));

        ofstream ofs("received/" + f, std::ios::trunc); //Just create the file to make sure it exists for truncate
        ofs.close();
        truncate(("received/" + f).c_str(), filelen);

        patientThreads.emplace_back(patient_file_thread_function, ref(request_buffer), m, p, f, filelen);

        for (int i = 0; i < w; i++) {
            TCPRequestChannel *newChannel = new TCPRequestChannel(host, port);
            channels.push_back(newChannel);
            workerThreads.emplace_back(worker_file_thread_function, ref(request_buffer), m, f, newChannel);
        }
    }
    else {
        for (int i = 1; i <= p; i++) {
            patientThreads.emplace_back(patient_thread_function, ref(request_buffer), n, i);
        }
        for (int i = 0; i < w; i++) {
            TCPRequestChannel *newChannel = new TCPRequestChannel(host, port);
            channels.push_back(newChannel);
            workerThreads.emplace_back(worker_thread_function, ref(request_buffer), ref(histogram_buffer), newChannel);
        }
        for (int i = 0; i < h; i++) {
            histogramThreads.emplace_back(histogram_thread_function, ref(histogram_buffer), ref(histograms),
                                          ref(histogramLocks));
        }
    }

    /* Join all threads here */
    for (thread &t: patientThreads) {
        t.join();
    }
    for (thread &t: workerThreads) {
        request_buffer.push(quitPacket); //Put quits for worker threads
    }
    for (thread &t: workerThreads) {
        t.join();
    }
    for (thread &t: histogramThreads) {
        histogram_buffer.push(quitPacket); //Put quits for histogram threads
    }
    for (thread &t: histogramThreads) {
        t.join();
    }

    gettimeofday (&end, 0);

    // print the results and time difference
	hc.print ();
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;
	
	// closing the channels
    Request q(QUIT_REQ_TYPE);
    for(TCPRequestChannel* channel : channels) {
        channel->cwrite(&q, sizeof(Request));
        delete channel;
    }
	// client waiting for the server process, which is the child, to terminate
	wait(0);
	cout << "Client process exited" << endl;
}
