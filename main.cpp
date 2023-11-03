#include <iostream>
#include <thread>
#include <string>
#include <unistd.h>
#include <signal.h>
#include <mutex>
#include <atomic>

// Message that both strings can access
std::string message;
std::mutex accessMessage;

// Alustetaan parametri tarkistamaan vastaanottajan lähettämä tila
static std::atomic_bool ACK(false);

// Counter for consecutive NACKs
static std::atomic_int nackCount(0);

// Store atomic var for receiving interrupt
static std::atomic_bool interrupt_received(false);

void modifyMessage(std::string msg) {
    std::lock_guard<std::mutex> lock(accessMessage);
    // Safely modify the shared string
    message = msg;
}

std::string readMessage() {
    std::lock_guard<std::mutex> lock(accessMessage);
    return message;
}

void getInput() {
    // Tallennetaan lähettäjän syöte
    std::string input;
    std::cout << "Please enter a message: ";
    std::getline(std::cin, input);
    modifyMessage(input);
}

// Lähettäjäsäie
void sender() {
    while (true) {
        sleep(1);
        if (interrupt_received.load()) {
            printf("Sender ending...\n");
            break;
        }
        // Count NACK's
        if (nackCount.load() > 10) {
            printf("Too many NACK's, ending program..\n");
            kill(0, SIGINT);
            break;
        }

        // Tarkistetaan vastaanottajan lähete
        if (ACK) std::cout << "ACK received." << std::endl;
        else {
            // Ask user CIN input
            getInput();
        }
    }
    return;
}

void ListenForExit(int sigID) {
    printf("\nExit caught...\n");
    interrupt_received.store(true);
    // Not ideal but if user input not provided, the sender will be caught in an infinite loop
    //kill(0, SIGTERM);
}
//
// Vastaanottajasäie
void receiver() {
    while (true) {
        sleep(1);
        if (interrupt_received.load()) {
            printf("Receiver ending...\n");
            break;
        }
        // Luetaan viesti 
        std::string receivedMessage = readMessage();

        // Jos ei null, viesti vastaanotettu
        if (!receivedMessage.empty()) {
            // Tulostetaan vastaanotettu viesti
            std::cout << "Vastaanotettu viesti: " << receivedMessage << std::endl;
            // Lähetä ack
            ACK.store(true);
        }
        else {
            // Viestiä ei vastaanotettu vielä, lähetetään NACK
            nackCount.store(nackCount += 1);
            //std::cout << nackCount.load() << std::endl;
        }
    }
    return;
}

int main() {
    // Register the custom signal handler for Ctrl+C
    signal(SIGINT, ListenForExit);
    // Start program
    printf("Starting Basestation and receiver. Press ctrl + c to exit.\n\n");

    // Lähettäjäsäie
    std::thread senderThread(sender);
    // Vastaanottajasäie
    std::thread receiverThread(receiver);

    // Odota, että lähettäjäsäie päättyy
    senderThread.join();
    // Odota, että vastaanottajasäie päättyy
    receiverThread.join();
    printf("Program exited cleanly\n");
    return 0;
}

