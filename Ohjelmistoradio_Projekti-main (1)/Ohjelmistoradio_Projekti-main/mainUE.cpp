// Authors: Oskari Mattila, Elias Räisänen, Annina Toimela, Oona Tujula, Serena Karjalainen, Jia Wenying

/** C++

compilation:

g++ -std=c++17 mainUE.cpp -lfftw3f -lfftw3 -luhd -lboost_system -lboost_program_options -pthread -orxTextMessage

*/


/**************************************************************
	Headers
**************************************************************/
#include "Headers.hpp"
#include "./PHY_UE.hpp"

// Data processing
#include "./Data_processing.hpp"


using namespace std;

static std::atomic<bool> STOP_SIGNAL_CALLED;
/**************************************************************
**************************************************************/



/**************************************************************
	Functions
**************************************************************/
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

  InterThreadQueueSDR7320<TXitem> tx_queue; 
  InterThreadQueueSDR7320<RXitem> rx_queue; 

  PHY_UE phyUE;
  phyUE.makeUEthread(tx_queue,rx_queue);

  std::cout<<"Starting main loop"<<std::endl;


  // random data generator
  int scaleSpectrum = RATE_SCALING;
  int NRBDL = 6*scaleSpectrum;
  int fft_size = 128*scaleSpectrum;
  RANDOM_DATA_FOR_PERFORMANCE_EST* testData = new RANDOM_DATA_FOR_PERFORMANCE_EST(fft_size,NRBDL);
  int nr_symbols = 6;
  int phy_packet_size = NRBDL*12/2 * nr_symbols;
 

  //*********************************************************************
  // Additional code
  //*********************************************************************

  bool no_err = false;
  bool data_received = false;

  //*********************************************************************
  //*********************************************************************

  int sendWindow=0;
  // while stop condition becomes true
  while(true){
    if(STOP_SIGNAL_CALLED.load()) break;

    std::this_thread::sleep_for(std::chrono::nanoseconds(9000));

    //***********************************************************************************
    // tx 
    //***********************************************************************************
    sendWindow++;
    if(!(sendWindow%10))
    {
      /* 
        Example data packet 
        Packet size - phy_packet_len bits  (int 0 or 1)            those are BPSK encoded in PHY   
        
        The example bits are binary bits of numbers from 0 - 99.  
      */
      {
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


      std::vector<int> tx_data(phy_packet_size);

      if (data_received) {

        if (no_err) {
          std::vector<int> header = to_int((Byte)0x06);
          for (int i = 0; i < phy_packet_size; i++) {
            if (i < 8) {
              tx_data[i] = header[i];
            }
            else if ( i < 16) {
              tx_data[i] = header[i - 8];
            }
            else {
              tx_data[i] = 0;
            }
          }
        }

        else {

          std::vector<int> header = to_int((Byte)0x15);
          for (int i = 0; i < phy_packet_size; i++) {
            if (i < 8) {
              tx_data[i] = header[i];
            }
            else if ( i < 16) {
              tx_data[i] = header[i - 8];
            }
            else {
              tx_data[i] = 0;
            }
          }
        }

      }

      no_err = false;
      data_received = false;

      auto txItem = std::unique_ptr<TXitem>(new TXitem());       // creates a send item that will hold the tx data
      txItem->insertData(std::move(tx_data));                     // put data into send item
      txItem->tx_meta_data.num_tx_symbols = 6;
      tx_queue.writeItem(std::move(txItem));                      // send the bits vector
      }

    }
    /**************************************************************
    **************************************************************/



    //***********************************************************************************      
    // rx
    //***********************************************************************************
    {
    auto read_rx_queue = std::move(rx_queue.readItem()); // reads the received data from PHY
		if(read_rx_queue != nullptr)                        // received a data packet
		{
        
        
      std::vector<int> rx_bin_data;
      for (int i = 0; i < phy_packet_size; i++) {
        rx_bin_data.push_back((int)(read_rx_queue.get()->elem[i]<0));
      }


      read_rx_queue.reset();
      

      //****************************************************************************************
      // Data processing
      //****************************************************************************************

      data_received = true;

      std::pair<std::vector<int>, std::vector<int>> header;
      for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 8; j ++) {
          if(i == 0)
            header.first.push_back(rx_bin_data[(i * 8) + j]);
          else
            header.second.push_back(rx_bin_data[(i * 8) + j]);
        }
      }

      if((to_byte(header.first) == 0x00) && (to_byte(header.second) == 0x00)) {
        
        std::vector<int> pay_nd_crc(rx_bin_data);

        pay_nd_crc.erase(pay_nd_crc.begin(), pay_nd_crc.begin() + (2 * 8));

        std::vector<Byte> pay_nd_crc_B = to_byte_v(pay_nd_crc);

        //CRC check:
        no_err = checkCRC16(&pay_nd_crc_B[0], (104 + 2)) == 0;
        

        // Print received message:
        if (no_err) std::cout << "Packet contained correct message: " << std::endl;
        else std::cout << "Packet was corrupted" << std::endl;
        for (auto it = pay_nd_crc_B.begin(); it != (pay_nd_crc_B.end() - 2); it++) {
          if (std::isprint((char)*it)) {
            std::cout << (char)*it;
          }

          else {
            if ((char)*it == '\0') {
              std::cout << " ";
            } else {
              std::cout << "*";
            }
          }

        }
        std::cout << std::endl << std::endl;
      }
      //****************************************************************************************
      //****************************************************************************************

    }
    }
    /**************************************************************
    **************************************************************/
    

 }


  // send stop to the phy thread
  phyUE.stopProcessing();
 
  std::this_thread::sleep_for(std::chrono::nanoseconds(100) ); // placeholder for processing

  std::cout<<"Exit the main function"<<std::endl;

  return retVal;
}
