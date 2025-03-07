#include <rtc/rtc.hpp> // WebRTC library
#include <nlohmann/json.hpp> // To interpret json files
#include <iostream> // To use the std::cout cmd
#include <thread> // To use receive information from both sockets simutaneosly
#include <atomic> // Special types built to work in parallel processing

using nlohmann::json;

int main() {

    // At first, I inicialized the peer connection and started to log the events
    rtc::InitLogger(rtc::LogLevel::Debug); // Started the logging at debug level
    auto pc = std::make_shared<rtc::PeerConnection>(); // Pointer to my PeerConnection
    

    // Now I need to start to build the offer

    // What command do I need to start gathering the SDP offer?
    // I remember that the example code uses something called gather state and onLocalDescription
    // The problem is that I don't really know why they start the local description.
    // Maybe by starting the peer connection pointer, it already start to gather the SDP offer.
    // I'll check what the rtc::PeerConnection() constructor usually does.
    // I'm not really sure, but it seems to be it. There are a lot of members in the file that sugest it

    // So I'll do a callback to print out the gathering state.
    // I have a doubt now. What's the difference between onLocalDescription, onLocalCandidate and onGatheringStateChange?
    // I'll check the references page.
    // They're not referenced in there, so I checked the media-sender example
    // They don't seem to use any of them.
    // Actually, they use onStateChange first.

    // Okay, so this block should print out the state of the SDP offer
    // It seems it's not the SDP offer state, it's actually the ICE state
    // I was wrong again. It's actually the state of the connection.
    pc->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "State of the connection: " << state << std::endl;
    });
    // When it finishes the ICE algorithym, it prints out the state as CLOSED

    // This one prints out the SDP state. If it's all built-up, then it says COMPLETE
    // I was wrong again. The gathering state is actually the state of the ICE  algorithym
    // When it's not started, it prints NEW, when it's happening, it prints IN_PROGRESS
    // When it finished, it prints COMPLETED
    pc->onGatheringStateChange([pc](rtc::PeerConnection::GatheringState state) {
        std::cout << "ICE State: " << state << std::endl;
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto local_offer = pc->localDescription();
            json message = {{"type", local_offer->typeString()},
                            {"sdp", }
            };
        }
    });

}
