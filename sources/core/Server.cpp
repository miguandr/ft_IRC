#include "../../includes/core/Server.hpp"


Server::Server(int port, std::string pass)
{
	this->_pass = pass;
	this->_port = port;
	this->_signalRecieved = false;
	this->_listeningSocket = -1;

	_registrationCommands["NICK"] = &Server::NICK;
	_registrationCommands["USER"] = &Server::USER;
	_registrationCommands["PASS"] = &Server::PASS;
	_registrationCommands["QUIT"] = &Server::QUIT;
	_channelCommands["JOIN"] = &Server::JOIN;
	_channelCommands["PART"] = &Server::PART;
	_channelCommands["PRIVMSG"] = &Server::PRIVMSG;
	_channelCommands["TOPIC"] = &Server::TOPIC;
	_channelCommands["INVITE"] = &Server::INVITE;
	_channelCommands["KICK"] = &Server::KICK;
	_channelCommands["MODE"] = &Server::MODE;
}

Server::Server(Server const &copy)
{
	this->_pass = copy._pass;
	this->_port = copy._port;
	this->_signalRecieved = copy._signalRecieved;
	this->_listeningSocket = copy._listeningSocket;
	this->_fds = copy._fds;
	this->_clients = copy._clients;
	this->_channels = copy._channels;
	this->_registrationCommands = copy._registrationCommands;
  	this->_channelCommands = copy._channelCommands;
}

Server& Server::operator=(Server const &copy)
{
	if(this != &copy)
	{
		this->_pass = copy._pass;
		this->_port = copy._port;
		this->_signalRecieved = copy._signalRecieved;
		this->_listeningSocket = copy._listeningSocket;
		this->_fds = copy._fds;
		this->_clients = copy._clients;
		this->_channels = copy._channels;
		this->_registrationCommands = copy._registrationCommands;
  		this->_channelCommands = copy._channelCommands;
	}
	return(*this);
}

Server::~Server()
{
	for(size_t i = 0; i < _clients.size(); i++)
		std::cout << YELLOW << "Client <" << _clients[i].get_fd()  << "> Disconnected" << RESET << std::endl;

	for (size_t i = 0; i < _fds.size(); i++)
		close(_fds[i].fd);

	_channels.clear();
	_clients.clear();
	_fds.clear();
	this->_listeningSocket = -1;
}


/******************/
/*     Methods    */
/******************/

/**
 * @brief Initializes the server socket and prepares it for listening incoming connections (socket).
 * @return void
 *
 * @details Creates and configures the TCP listening socket:
 * - Creates TCP IPv4 socket for incoming connections
 * - Sets SO_REUSEADDR to avoid "Address already in use" errors (setsockopt)
 * - Configures non-blocking mode for accept() operations (fcntl)
 * - Binds socket to specified port on all interfaces (bind)
 * - Starts listening for incoming connections (listen)
 * - Defines the address and port where the server will accept connections (sockaddr_in addr)
 * - Adds listening socket to poll monitoring array (pollfd listenPollFd)
 *
 * @throws std::runtime_error If socket creation, configuration, or binding fails
 * @note
 *	socket --> creates a new TCP IPv4 socket. Its return value is a fd. This socket is the entry point for clients
 *	setsockopt --> to avoid “Address already in use” error when quickly restarting the server
 *	fcntl --> changes the socket mode so that read/write operations do not block the process
 *	sockaddr_in addr --> struct used to indicate the IP address and port where the socket will listen
 *	bind --> associates the socket with the IP address and port set in the addr struct
 *	listen --> puts the socket into listening mode for incoming connections
 * @see execute() for the main server loop that uses this socket
 */
void Server::init()
{
	//1. Creates a new socket (fd) that uses the IPv4 address and the TCP protocol (to send/receive data reliably)
	this->_listeningSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (_listeningSocket < 0)
		throw(std::runtime_error("Failed to create socket"));

	int enable = 1; //1 = true
	if (setsockopt(_listeningSocket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
		throw(std::runtime_error("Failed to set SO_REUSEADDR on listening socket"));

	//2. when accept() is called and there are no connections waiting, instead of blocking, it returns an error.
	if (fcntl(_listeningSocket, F_SETFL, O_NONBLOCK) < 0)
		throw(std::runtime_error("Failed to set non-blocking mode on listening socket"));

	//3. Create a new sockaddr_in structure element to specify which IP address and port this socket should be ‘bound’ to
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET; // Type of adress: IPv4
	addr.sin_port = htons(this->_port); //the port that the listening socket will use, converted to network byte order (big endian)
	addr.sin_addr.s_addr = INADDR_ANY; //Listen on all interfaces (all IPs of the server)
	memset(addr.sin_zero, 0, sizeof(addr.sin_zero)); //Clears the padding bytes to avoid garbage in the structure

	//4. Bind _listeningSocket to the IP and port specified in addr
	if (bind(_listeningSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0)
		throw(std::runtime_error("Failed to bind socket"));

	//5. Set the socket to listening mode for incoming connections
	if (listen(_listeningSocket, SOMAXCONN) < 0)
		throw(std::runtime_error("Listen failed"));

	//6. new node of the pollfd struct to add to the struct _fds. Here, it is configured how the poll function should behave with the assigned socket (the listening socket)
	struct pollfd listenPollFd;
	listenPollFd.fd = this->_listeningSocket; //The socket to monitor: the listening socket
	listenPollFd.events = POLLIN; //Events of interest: when there are new pending connections
	listenPollFd.revents = 0; //Occurred events: initialized to zero

	_fds.push_back(listenPollFd);
}

/**
 * @brief Main server execution loop using poll() for handling multiple clients.
 * @return void
 *
 * @details Monitors all sockets for activity and dispatches events:
 * - Uses poll() to wait for activity on any monitored socket
 * - Handles new client connections on listening socket
 * - Processes incoming data from existing clients
 * - Continues until signal is received to stop server
 *
 * @throws std::runtime_error If poll() system call fails
 * @see NewClient() for handling new connections
 * @see NewData() for processing client data
 */
void Server::execute()
{
	while (_signalRecieved == false)
	{
		if((poll(&_fds[0], _fds.size(), -1) < 0) && _signalRecieved == false) //timeout = -1 espera indefinidamente
			throw(std::runtime_error("poll failed"));

		if(_signalRecieved)
			break;

		for(size_t i = 0; i < _fds.size(); i++)
		{
			if(_fds[i].revents & POLLIN)
			{
				if(_fds[i].fd == _listeningSocket)
					NewClient();
				else
					NewData(_fds[i].fd);
			}
		}

		// Procesar clientes marcados para QUIT
		std::vector<Client>::iterator it;
		for(it = _clients.begin(); it != _clients.end(); it++)
		{
		    if (it->get_isQuitting())
		    {
		        ft_close(it->get_fd());
		        std::cout << YELLOW << "Client fd " << it->get_fd() << " disconnected\n" << RESET;
		        break;
		    }
		}
	}
}

/**
 * @brief Accepts a new client connection and creates a Client object.
 * @return void
 *
 * @details Handles the complete process of accepting new connections:
 * - Accepts incoming connection on listening socket (accept)
 * - Sets new socket to non-blocking mode (fcntl)
 * - Creates new node of the pollfd struct for the new Client instance with socket details
 * - Adds client to monitoring list with poll()
 * - Logs connection event for debugging
 *
 * @throws std::runtime_error If accept() fails or socket configuration fails
 * @note Client begins in unregistered state and must complete authentication
 * - accept --> Extracts the first pending connection from the listening socket's queue and
 *              returns a new socket file descriptor connected to the client.
 * - fcntl --> Sets the newly accepted client socket to non-blocking mode so that read/write
 *             operations will not block the server loop.
 * @see Client() constructor for initial client setup
 */
void Server::NewClient()
{
	struct sockaddr_in clientAddr;
	memset(&clientAddr, 0, sizeof(clientAddr));
	socklen_t addrLen = sizeof(clientAddr);
	int clientSocket = accept(_listeningSocket, (struct sockaddr*)&clientAddr, &addrLen);
	if (clientSocket < 0)
		throw(std::runtime_error("Failed to accept a client"));

	//1. Set the client socket to non-blocking mode”
	if (fcntl(clientSocket, F_SETFL, O_NONBLOCK) < 0)
		throw(std::runtime_error("Failed to set non-blocking mode on client socket"));

	//2. new pollfd node to add to the _fds vector
	struct pollfd newClientPollFd;
	newClientPollFd.fd = clientSocket; //the socket to monitor: clientSocket
	newClientPollFd.events = POLLIN; //Events of interest: data sent by the client
	newClientPollFd.revents = 0; //Occurred events: initialized to zero.
	_fds.push_back(newClientPollFd);

	//3. new client node to add to the _clients vector
	Client newClient;
	newClient.set_fd(clientSocket);
	newClient.set_IPaddress(inet_ntoa((clientAddr.sin_addr))); //inet_ntoa --> Convert the binary IPv4 address (in_addr) into a readable string
	_clients.push_back(newClient);

	std::cout << YELLOW << "Client connected: fd " << clientSocket << RESET << std::endl;
}

/**
 * @brief Reads and processes data from an existing client connection and decides what to do with it.
 * @param clientFd The file descriptor of the client socket to read from
 * @return void
 *
 * @details Handles all aspects of client data processing:
 * - Receives data from client socket using recv()
 * - Detects client disconnections (recv returns 0)
 * - Handles socket errors and close the socket and remove it from _fds.
 * - Accumulates partial IRC messages in client buffer
 * - Parses complete messages and executes IRC commands
 * - Manages client cleanup on disconnection or errors
 *
 * @throws std::runtime_error If socket operations fail unexpectedly
 * @note IRC messages may arrive in multiple packets and need buffering
 * @see parser() for IRC message parsing logic
 */
void Server::NewData(int clientFd)
{
	char buffer[1024];
	memset(buffer, 0, sizeof(buffer));
	size_t bytesReceived = recv(clientFd, buffer, sizeof(buffer) - 1, 0);

	if (bytesReceived <= 0) //The client closed the connection or an error occurred
	{
		std::cerr << RED << "Connection closed or error on client's fd " << clientFd << RESET << std::endl;
		ft_close(clientFd);
		return;
	}
	buffer[bytesReceived] = '\0';

	Client* currentClient = this->get_client(clientFd);
	if (!currentClient)
		throw std::runtime_error("error client doesn't exist");

	//1. Accumulate the received data in the client’s private buffer, DO NOT overwrite
	currentClient->set_buffer(buffer);
	const std::string& accumulatedBuffer = currentClient->get_buffer();
	//std::cout << "DEBUG: Accumulated buffer for fd " << clientFd << ": " << GREEN << accumulatedBuffer << RESET << std::endl; //💡 to test fragmented commands

	//2. Check if the accumulated buffer contains one or more complete commands ending with \r\n
	if (accumulatedBuffer.find("\r\n") == std::string::npos)
		return; //If not found, return to poll() to wait for more data; it is not the end of the IRC command.

	//3. Split all complete commands in the accumulated buffer
	std::vector<std::string> commands = split_receivedBuffer(accumulatedBuffer);
	currentClient->set_cmd(commands); //Each command is always delimited by \r\n

	//4. Parse each command
	for (size_t i = 0; i < currentClient->get_cmd().size(); i++)
		this->parser(currentClient->get_cmd()[i], clientFd);

	//5. Clean the client's buffer after parsing
	currentClient->clearBuffer();
}

/**
 * @brief Parses and executes IRC commands received from clients.
 * @param command The raw IRC command string received from client
 * @param fd The file descriptor of the client who sent the command
 * @return void
 *
 * @details Implements complete IRC command processing pipeline:
 * - Normalizes and validates command format
 * - Splits command into tokens (command + parameters)
 * - Converts command to uppercase for case-insensitive matching
 * - Maps command strings to appropriate command handler objects
 * - Executes command with proper authentication checks
 * - Supports all IRC commands: PASS, NICK, USER, JOIN, PART, PRIVMSG, etc.
 *
 * @note Commands are processed through Command Pattern for maintainability
 * @note Invalid commands are silently ignored (IRC specification)
 * @see ICommand interface for command implementation structure
 * @see split_cmd() for command tokenization logic
 */
void Server::parser(const std::string &command, int fd)
{
	std::string cmd = normalize_param(command, false);
	if(cmd.empty())
		return;

	std::vector<std::string> commands = split_cmd(cmd);

	//1. normalize letters from command token to capital letters
	for (size_t i = 0; i < commands[0].size(); i++)
		commands[0][i] = toupper(commands[0][i]);

	std::string cmdName = commands[0];

	//2. Check command registration
	std::map<std::string, CommandHandler>::iterator it = _registrationCommands.find(cmdName);
	if (it != _registrationCommands.end())
	{
		CommandHandler handler = it->second;
		(this->*handler)(cmd, fd);
		return;
	}

	//3. Check if the user is registered for channel commands and is logged in
	if (isregistered(fd) && get_client(fd)->get_logedIn())
	{
		std::map<std::string, CommandHandler>::iterator it2 = _channelCommands.find(cmdName);
		if (it2 != _channelCommands.end())
		{
			CommandHandler handler = it2->second;
			(this->*handler)(cmd, fd);
		}
		else
			_sendResponse(ERROR_COMMAND_NOT_RECOGNIZED(get_client(fd)->get_nickname(), cmdName), fd);
	}
	else
		_sendResponse(ERROR_NOT_REGISTERED_YET(std::string("*")), fd);
}


// Method to split the buffer using the "\r\n" delimiter

/**
 * @brief Splits received data buffer into individual IRC commands.
 * @param buffer The accumulated data buffer from client
 * @return std::vector<std::string> Vector of complete IRC command strings
 *
 * @details Handles IRC protocol message framing:
 * - Searches for IRC line terminators ("\\r\\n")
 * - Extracts complete commands from accumulated buffer
 * - Normalizes each command by removing extra whitespace
 * - Handles partial messages that span multiple recv() calls
 * - Returns only complete, properly terminated commands
 *
 * @note IRC protocol requires commands to end with \\r\\n (CRLF)
 * @note Incomplete commands remain in client buffer for next processing
 * @see normalize_param() for command string cleanup
 */
std::vector<std::string> Server::split_receivedBuffer(std::string buffer)
{
	std::vector<std::string> commands;
	std::string line;
	size_t start = 0;
	size_t end;

	//Search while there is "\r\n"
	while ((end = buffer.find("\r\n", start)) != std::string::npos)
	{
		line = normalize_param(buffer.substr(start, end - start), false);
		if (!line.empty())
			commands.push_back(line);
		start = end + 2; //skip "\r\n"
	}

	start = 0;
	while ((end = buffer.find("\n", start)) != std::string::npos)
	{
		// Skip if this \n is part of \r\n (already processed above)
		if (end > 0 && buffer[end-1] == '\r') {
			start = end + 1;
			continue;
		}
		line = normalize_param(buffer.substr(start, end - start), false);
		if (!line.empty())
			commands.push_back(line);
		start = end + 1; //skip "\n"
	}
	return commands;
}

void Server::addChannel(Channel newChannel){this->_channels.push_back(newChannel);}


/*****************/
/*    Getters    */
/*****************/
Client* Server::get_client(int fd)
{
	for (size_t i = 0; i < _clients.size(); i++)
	{
		if (_clients[i].get_fd() == fd) {
			return &_clients[i];
		}
	}
	return NULL;
}

Client *Server::get_clientNick(std::string nickname)
{
	for (size_t i = 0; i < this->_clients.size(); i++)
	{
		if (this->_clients[i].get_nickname() == nickname)
			return &this->_clients[i];
	}
	return NULL;
}

Channel* Server::get_channelByName(const std::string& name)
{
	for (std::vector<Channel>::iterator it = _channels.begin(); it != _channels.end(); ++it)
	{
		if (it->get_name() == name)
			return &(*it);
	}
	return NULL;
}
