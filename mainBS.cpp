// Basestation part of simple chat interface between two users.
// C++

// Line for compilation:

g++ -std=c++17 mainBS.cpp -lfftw3f -lfftw3 -luhd -lboost_system -lboost_program_options -pthread -otxTextMessage

// Headers, remove comments when necessary. 

//#include "headers.hpp"
//#include "mainHeaders.hpp"
//#include "Headers.hpp"
//#include "./PHY_BS.hpp"


// Data processing
#include "./Data_processing.hpp"

using namespace std;

static std::atomic<bool> STOP_SIGNAL_CALLED;

// Below: builtin signal handler to handle incoming signals

void os_signal_handler(int s){
  printf("\n Caught signal %d\n",s);
  STOP_SIGNAL_CALLED.store(true);
}

// Main function starts here:

int UHD_SAFE_MAIN(int argc, char *argv[])
{
  int retVal = EXIT_SUCCESS;

  std::signal(SIGINT, os_signal_handler);

  // Queues for communicating between threads. These will store the message to be sent (in a sort of buffer)

  InterThreadQueueSDR7320<TXitem> txQueue;    // TX queue from the main to PHY thread
  InterThreadQueueSDR7320<RXitem> rxQueue;    // RX queue from the PHY thread to main

  PHY_BS phyBS;                               // Create BS instance
  phyBS.makeBSthread(txQueue,rxQueue);        // Start BS thread

  // random data generator. What does this do?

  int scaleSpectrum = 4;
  int NRBDL = 6*scaleSpectrum;
  int fft_size = 128*scaleSpectrum;
  RANDOM_DATA_FOR_PERFORMANCE_EST* testData = new RANDOM_DATA_FOR_PERFORMANCE_EST(fft_size,NRBDL);
  int nr_symbols = 6;
  int phy_packet_size = NRBDL*12/2 * nr_symbols;

  // These three are necessary when transmitting our message and listening to incoming ACK's or NACK's:

  bool transmit_on = true;
  const int retransmit_numb = 500;
  int retransmit_count = retransmit_numb;

  // The comments below mention the binary forms of ACK and NACK transmissions.

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

  std::string message = "Hello world!"; // init parameter "message" as "Hello world!"
  int message_len = message.length(); // store length of message in parameter for future use
  const char * msg = message.c_str(); // converts the string parameter "message" into an array of characters, "msg"

  std::vector<Byte> payload_B(104);

  for (int i = 0; i < message_len; i++) {
    payload_B[i] = (Byte)msg[i];
  }

  const Byte *pay_B = &payload_B[0];

  uint32_t checksum = computeCRC16(pay_B, payload_B.size());

  std::vector<int> checksum_bin(2 * 8);
  for(int i = 15; i >= 0; i--) {   // 2 bytes = 16 bits
    checksum_bin[i] = checksum & 1;
    checksum = checksum >> 1;
  }


  std::vector<int> pay_bin(104*8);
  for (int i = 0; i < 104; i++) {
    std::vector<int> new_Byte = to_int(pay_B[i]);
    for (int j = 0; j < 8; j++) {
      pay_bin[(i * 8) + j] = new_Byte[j];
    }
  }

  std::vector<int> txMsg(std::vector<int> (2 * 8));
  txMsg.insert(txMsg.end(), pay_bin.begin(), pay_bin.end());
  txMsg.insert(txMsg.end(), checksum_bin.begin(), checksum_bin.end());

  // This is the part where the message is sent and the basestation waits for and ACK or NACK, 
  // everything above was initialising the procedure.

  std::cout<<"BS main loops"<<std::endl; 
  int waitForTx=0; // init waitForTx to 0, enables the loop to break at some point
  while(true){
    if(STOP_SIGNAL_CALLED.load()) break;      // catch ctrl-C and end the code cleanly

    std::this_thread::sleep_for(std::chrono::nanoseconds(5000));

    waitForTx++;
    if(!(waitForTx%10))
    {
      {
      if (transmit_on) {

        std::vector<int> txData(txMsg);         // Copy encoded message to txData vector

        // Print sent data packet, comment out if needed:
      
        std::cout << "Sent data packet: " << std::endl;
        for (auto it = txData.begin(); it != txData.end(); it++) {
          std::cout << *it << " ";
        }
        std::cout << std::endl << std::endl;
    
        auto txItem = std::unique_ptr<TXitem>(new TXitem());       // creates a send item that will hold the tx data
        txItem->insertData(std::move(txData));                     // put data into send item
        txItem->tx_meta_data.num_tx_symbols = 6;
        txQueue.writeItem(std::move(txItem));                      // send the bits vector
      }

      else {
      std::vector<int> txData;
      for (int i = 0; i < phy_packet_size; i++) {
        if (i%2) txData.push_back(0);
        else txData.push_back(1);
      }
      auto txItem = std::unique_ptr<TXitem>(new TXitem());       // creates a send item that will hold the tx data
      txItem->insertData(std::move(txData));                     // put data into send item
      txItem->tx_meta_data.num_tx_symbols = 6;
      txQueue.writeItem(std::move(txItem));                      // send the bits vector
      }

      if (transmit_on) retransmit_count = retransmit_numb;
      transmit_on = false;

      if (retransmit_count < 0) {
        transmit_on = true;
        retransmit_count = retransmit_numb;
        std::cout << "Response took too long resending packet!" << std::endl << std::endl;
      }
      else retransmit_count--;
      }
    }
    {
    auto tmp = std::move(rxQueue.readItem()); // reads the received data from PHY
		if(tmp != nullptr)                        // received a data packet
    {

      std::vector<int> rx_bin_data;
      for (int i = 0; i < phy_packet_size; i++) {
        rx_bin_data.push_back((int)(tmp.get()->elem[i]<0));
      }

      tmp.reset();

      std::pair<std::vector<int>, std::vector<int>> header;
      for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 8; j ++) {
          if(i == 0)
            header.first.push_back(rx_bin_data[(i * 8) + j]);
          else
            header.second.push_back(rx_bin_data[(i * 8) + j]);
        }
      }

      // Print sent data packet, comment out if needed:

      std::cout << "Received response: " << std::endl;
      for (auto it = header.first.begin(); it != header.first.end(); it++) {
        std::cout << *it << " ";
      }
      std::cout << std::endl;
      for (auto it = header.second.begin(); it != header.second.end(); it++) {
        std::cout << *it << " ";
      }
      std::cout << std::endl << std::endl;
    

      if((to_byte(header.first) == 0x06) && (to_byte(header.second) == 0x06)) {
        std::cout << "Message sent succesfully! ACK received. Ending program..." << std::endl << std::endl;
        break;
      }

      else if((to_byte(header.first) == 0x15) && (to_byte(header.second) == 0x15)) {
        std::cout << "Message sent unsuccesfully! Resending packet." << std::endl << std::endl;
        transmit_on = true;
      }
    }
    }
  }

  // send stop to the phy thread

  phyBS.stopProcessing();

  delete testData;

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cout<<"Exit the main function"<<std::endl;

  return retVal;
}
