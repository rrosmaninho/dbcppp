#include <regex>
#include <array>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

#include <cxxopts.hpp>
#include <nlohmann/json.hpp>

#include "dbcppp/Network.h"
#include "dbcppp/Network2Functions.h"

using json = nlohmann::json;

void print_help()
{
    std::cout << "dbcppp v1.0.0\nFor help type: dbcppp <subprogram> --help\n"
              << "Sub programs: dbc2, decode\n";
}

int main(int argc, char** argv)
{
    cxxopts::Options options("dbcppp", "");
    if (argc < 2 || std::string("help") == argv[1])
    {
        print_help();
        return 1;
    }

    if (std::string("dbc2") == argv[1])
    {
        options.add_options()
            ("h,help", "Produce help message")
            ("f,format", "Output format (C, DBC, human)", cxxopts::value<std::string>())
            ("dbc", "List of DBC files", cxxopts::value<std::vector<std::string>>());

        for (std::size_t i = 1; i < argc - 1; i++)
        {
            argv[i] = argv[i + 1];
        }
        auto vm = options.parse(argc - 1, argv);

        if (vm.count("help"))
        {
            std::cout << "Usage:\ndbcppp dbc2c [--help] --format=<format>... --dbc=<dbc filename>...\n";
            std::cout << options.help();
            return 1;
        }
        if (!vm.count("format"))
        {
            std::cout << "Argument error: Argument --format=<format> missing\n";
            return 1;
        }
        if (!vm.count("dbc"))
        {
            std::cout << "Argument error: At least one --dbc=<dbc> argument required\n";
            return 1;
        }
        const auto& format = vm["format"].as<std::string>();
        auto dbcs = vm["dbc"].as<std::vector<std::string>>();
        auto net = dbcppp::INetwork::Create({}, {}, dbcppp::IBitTiming::Create(0, 0, 0), {}, {}, {}, {}, {}, {}, {}, {});
        for (const auto& dbc : dbcs)
        {
            auto nets = dbcppp::INetwork::LoadNetworkFromFile(dbc);
            for (auto& other : nets)
            {
                net->Merge(std::move(other.second));
            }
        }
        if (format == "C")
        {
            using namespace dbcppp::Network2C;
            std::cout << *net;
        }
        else if (format == "DBC")
        {
            using namespace dbcppp::Network2DBC;
            std::cout << *net;
        }
        else if (format == "human")
        {
            using namespace dbcppp::Network2Human;
            std::cout << *net;
        }
    }
    else if (std::string("decode") == argv[1])
    {
        options.add_options()
            ("h,help", "Produce help message")
            ("bus", "List of buses in format <<bus name>:<DBC filename>>", cxxopts::value<std::vector<std::string>>())
            ("json", "Output in JSON format");

        for (std::size_t i = 1; i < argc - 1; i++)
        {
            argv[i] = argv[i + 1];
        }

        auto vm = options.parse(argc, argv);
        if (vm.count("help"))
        {
            std::cout << "Usage:\ndbcppp decode [--help] --bus=<<bus name>:<DBC filename>> [--json]\n";
            std::cout << options.help();
            return 1;
        }
        if (!vm.count("bus"))
        {
            std::cout << "Argument error: At least one --bus=<<bus name>:<DBC filename>> argument required\n";
            return 1;
        }

        bool json_output = vm.count("json");

        const auto& opt_buses = vm["bus"].as<std::vector<std::string>>();
        struct Bus
        {
            std::string name;
            std::unique_ptr<dbcppp::INetwork> net;
        };
        std::unordered_map<std::string, Bus> buses;

        // Load buses and networks
        for (const auto& opt_bus : opt_buses)
        {
            std::istringstream ss(opt_bus);
            std::string opt;
            Bus b;
            if (std::getline(ss, opt, ':'))
            {
                b.name = opt;
            }
            else
            {
                std::cout << "error: could not parse bus parameter" << std::endl;
                return 1;
            }
            if (std::getline(ss, opt))
            {
                std::ifstream fdbc(opt);
                b.net = dbcppp::INetwork::LoadDBCFromIs(fdbc);
                if (!b.net)
                {
                    std::cout << "error: could not load DBC '" << opt << "'" << std::endl;
                    return 1;
                }
            }
            else
            {
                std::cout << "error: could not parse bus parameter" << std::endl;
                return 1;
            }
            buses.insert(std::make_pair(b.name, std::move(b)));
        }

        std::regex regex_candump_line(
            "^\\s*(\\S+)"
            "\\s*([0-9A-F]{3})"
            "\\s*\\[(\\d+)\\]"
            "\\s*([0-9A-F]{2})?"
            "\\s*([0-9A-F]{2})?"
            "\\s*([0-9A-F]{2})?"
            "\\s*([0-9A-F]{2})?"
            "\\s*([0-9A-F]{2})?"
            "\\s*([0-9A-F]{2})?"
            "\\s*([0-9A-F]{2})?"
            "\\s*([0-9A-F]{2})?");
        
        std::string line;
        while (std::getline(std::cin, line))
        {
            std::cmatch cm;
            std::regex_match(line.c_str(), cm, regex_candump_line);
            const auto& bus = buses.find(cm[1].str());
            if (bus != buses.end())
            {
                uint64_t msg_id = std::strtol(cm[2].str().c_str(), nullptr, 16);
                uint64_t msg_size = std::atoi(cm[3].str().c_str());
                std::array<uint8_t, 8> data;
                for (std::size_t i = 0; i < msg_size; i++)
                {
                    data[i] = uint8_t(std::strtol(cm[4 + i].str().c_str(), nullptr, 16));
                }

                auto beg_msg = bus->second.net->Messages().begin();
                auto end_msg = bus->second.net->Messages().end();
                auto iter = std::find_if(beg_msg, end_msg, [&](const dbcppp::IMessage& msg) { return msg.Id() == msg_id; });
                if (iter != end_msg)
                {
                    const dbcppp::IMessage* msg = &*iter;

                    if (json_output)
                    {
                        json json_output;
                        json_output["bus"] = cm[1].str();
                        json_output["message_id"] = msg_id;
                        json_output["message_name"] = msg->Name();
                        json_output["signals"] = json::object();

                        for (const dbcppp::ISignal& sig : msg->Signals())
                        {
                            auto raw = sig.Decode(&data[0]);
                            auto iter = std::find_if(
                                sig.ValueEncodingDescriptions().begin(),
                                sig.ValueEncodingDescriptions().end(),
                                [&](const dbcppp::IValueEncodingDescription& ved) { return ved.Value() == raw; });

                            if (iter != sig.ValueEncodingDescriptions().end())
                            {
                                json_output["signals"][sig.Name()] = iter->Description();
                            }
                            else
                            {
                                json_output["signals"][sig.Name()] = sig.RawToPhys(raw);
                            }
                        }

                        // Print JSON
                        std::cout << json_output.dump(4) << std::endl;
                    }
                    else
                    {
                        // Normal human-readable output
                        bool first = true;
                        for (const dbcppp::ISignal& sig : msg->Signals())
                        {
                            auto raw = sig.Decode(&data[0]);
                            if (!first) std::cout << ", ";
                            std::cout << sig.Name() << ": " << sig.RawToPhys(raw);
                            first = false;
                        }
                        std::cout << std::endl;
                    }
                }
            }
        }
    }
    else
    {
        print_help();
        return 1;
    }
}

