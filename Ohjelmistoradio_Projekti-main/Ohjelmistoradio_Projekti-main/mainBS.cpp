/*
  simple tx rx test 
 - pregenerates samples
 - repeats same transmission
 - records samples 
 */

/** C++

compilation:

g++ -std=c++17 mainBS.cpp -lfftw3f -lfftw3 -luhd -lboost_system -lboost_program_options -pthread -otxTextMessage

*/

/**************************************************************
	Headers
**************************************************************/
//#include "mainHeaders.hpp"
#include "Headers.hpp"

#include "./PHY_BS.hpp"

// Data processing
#include "./Data_processing.hpp"

using namespace std;

static std::atomic<bool> STOP_SIGNAL_CALLED;

void os_signal_handler(int s){
  printf("\n Caught signal %d\n",s);
  STOP_SIGNAL_CALLED.store(true);
}



/**************************************************************
	Main
**************************************************************/
int UHD_SAFE_MAIN(int argc, char *argv[]) 
{
  int retVal = EXIT_SUCCESS;

  std::signal(SIGINT, os_signal_handler);

  // Queues for communicating between threads
  InterThreadQueueSDR7320<TXitem> txQueue;    // TX queue from the main to PHY thread
  InterThreadQueueSDR7320<RXitem> rxQueue;    // RX quewe from the PHY thread to main

  PHY_BS phyBS;                 // Create BS instance 
  phyBS.makeBSthread(txQueue,rxQueue);        // Start BS thread

  // random data generator
  int scaleSpectrum = 4;
  int NRBDL = 6*scaleSpectrum;
  int fft_size = 128*scaleSpectrum;
  RANDOM_DATA_FOR_PERFORMANCE_EST* testData = new RANDOM_DATA_FOR_PERFORMANCE_EST(fft_size,NRBDL);
  int nr_symbols = 6;
  int phy_packet_size = NRBDL*12/2 * nr_symbols;

  size_t packet_no = 1;
  bool transmit_on = true;
  const int retransmit_numb = 500;
  int retransmit_count = retransmit_numb;

  //******************************************************************
  // Data packet:
  //******************************************************************

  // 2 byte header, 104 byte payload and 2 byte crc. 1 packet = 108 bytes = 864 bits.
  //
  // Header symbols:
  //
  // symbol:    hex:     bin:          comment:
  //
  // ACK        0x06     00000110
  // NAC        0x15     00010101
  //
  // NUL        0x00     00000000      Data packet

  std::string message = "This is our message!!";
  if (argc > 1) {
    message = "";                               // Optionally pass input message as command line argument
    for (int i = 1; i < argc; i++) {
      message.append(argv[i]);
      message.append(" ");
    }
  }

  std::vector<int> txMsg = MSG_to_bin(message);

  //******************************************************************
  //******************************************************************

  //******************************************************************
  //******************************************************************



  std::cout<<"BS main loops"<<std::endl;
  int waitForTx=0;
  while(true){
    if(STOP_SIGNAL_CALLED.load()) break;      // catch ctrl-C and end the code cleanly

    std::this_thread::sleep_for(std::chrono::nanoseconds(5000));

    //********************************************************************
    // tx 
    //********************************************************************
    waitForTx++;
    if(!(waitForTx%100))
    {
      /* 
        Example data packet 
        Packet size - 144 bits  (int 0 or 1)            those are BPSK encoded in PHY   
        
        The example bits are binary bits of numbers from 0 - 99.  
      */

      if (transmit_on) {

        std::vector<int> txData(txMsg);         // Copy encoded message to txData vector

        auto txItem = std::unique_ptr<TXitem>(new TXitem());       // creates a send item that will hold the tx data
        txItem->insertData(std::move(txData));                     // put data into send item
        txItem->tx_meta_data.num_tx_symbols = 6;
        txQueue.writeItem(std::move(txItem));                      // send the bits vector
        std::cout << "Packet sent with message \"" << message << "\" (packet no: " << packet_no << ")!" << std::endl << std::endl << std::endl;
        packet_no ++;

      }

      if (transmit_on) retransmit_count = retransmit_numb;
      transmit_on = false;

      if (retransmit_count < 0) {
        transmit_on = true;
        retransmit_count = retransmit_numb;
        std::cout << "Response took too long resending packet!" << std::endl << std::endl << std::endl;
      }
      else retransmit_count--;

    }
    /**************************************************************
    **************************************************************/



    //********************************************************************
    // rx 
    //********************************************************************
    {
    auto tmp = std::move(rxQueue.readItem()); // reads the received data from PHY
		if(tmp != nullptr)                        // received a data packet
    {
      
      std::vector<int> rx_bin_data;
      for (int i = 0; i < phy_packet_size; i++) {
        rx_bin_data.push_back((int)(tmp.get()->elem[i]<0));
      }


      tmp.reset();
      

      //****************************************************************************************
      // Data processing
      //****************************************************************************************
      std::pair<std::vector<int>, std::vector<int>> header;
      for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 8; j ++) {
          if(i == 0)
            header.first.push_back(rx_bin_data[(i * 8) + j]);
          else
            header.second.push_back(rx_bin_data[(i * 8) + j]);
        }
      }

      /*
      // Printing affetcs processing time of rx
      std::cout << "Received response: " << std::endl;
      for (auto it = header.first.begin(); it != header.first.end(); it++) {
        std::cout << *it << " "; 
      }
      std::cout << std::endl;
      for (auto it = header.second.begin(); it != header.second.end(); it++) {
        std::cout << *it << " "; 
      }
      std::cout << std::endl << std::endl;
      */
      
      

      if((to_byte(header.first) == 0x06) && (to_byte(header.second) == 0x06)) {
        std::cout << "ACK received: Message sent succesfully!" << std::endl;
        std::cout << "Input new message:" << std::endl << std::endl;
        char c_message[105];
        cin.get(c_message, 105);
        message = (std::string)c_message;
        cin.clear();
        cin.ignore(100000, '\n');
        txMsg = MSG_to_bin(message);
        std::cout << std::endl << std::endl;
        transmit_on = true;
      }

      else if((to_byte(header.first) == 0x15) && (to_byte(header.second) == 0x15)) {
        std::cout << "NACK received: Message sent unsuccesfully!" << std::endl << std::endl << std::endl;
        transmit_on = true;
      }
      //****************************************************************************************
      //****************************************************************************************

    }
    }
    /**************************************************************
    **************************************************************/
  

  }



  // send stop to the phy thread
  phyBS.stopProcessing();
  
  delete testData;
  
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
 
  std::cout<<"Exiting main function..."<<std::endl;
  std::cout<<"Bye! :)"<<std::endl;

  return retVal;
}
