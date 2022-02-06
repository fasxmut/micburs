//
// Copyright (c) 2022 Fas Xmut (fasxmut at protonmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <iostream>
#include <memory>
#include <boost/beast.hpp>
#include <botan/asio_stream.h>
#include <botan/certstor_system.h>
#include <botan/auto_rng.h>
#include <string>
#include <string_view>
#include <vector>
#include <boost/signals2.hpp>
#include <irrlicht.h>
#include <exception>
#include <functional>
#include <SFML/Audio.hpp>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace ip = asio::ip;
using ip::tcp;
namespace http = beast::http;
namespace TLS = Botan::TLS;
using namespace std::string_literals;
using namespace std::placeholders;

enum class Color: std::uint32_t {
	Grey = 0xff141414, // Program Started
	LightBlue = 0xff6762f3, // Host Resolved
	DarkBlue = 0xff070730, // Host Connected
	NGreen = 0xff327231, // TLS Handshaked
	NYellow = 0xff767552, // Http Requested
	NLight = 0xffc2c9cf, // Got: Http Responsed
	Red1 = 0xfff23079, // Network Exception
	Red2 = 0xffff0000, // Cpp General Exception
	NCyan = 0xff22686a,
	none
};

enum {
	menuid_start,
	menuid_about,
	menuid_quit,
	barid_start,
	barid_quit,
	buttonid_start,
	buttonid_quit
};

class DD final {};
DD dd;

class CredentialsManager: public Botan::Credentials_Manager {
private:
	Botan::System_Certificate_Store systemCertificateStore;
public:
	std::vector<Botan::Certificate_Store *> trusted_certificate_authorities(
		const std::string &,
		const std::string &
	) override {
		return {&systemCertificateStore};
	}
};

class SigMan {
public:
	enum class NetStat {
		ProgramStarted,
		Resolved,
		Connected,
		Handshaked,
		Requested,
		Got,
		PleaseClose,
		NetworkException,
		CppGeneralException
	};
	static std::string NetStatusString(SigMan::NetStat stat) {
		if (stat == NetStat::ProgramStarted)
			return "SigMan::NetStat::ProgramStarted";
		else if (stat == NetStat::Resolved)
			return "SigMan::NetStat::Resolved";
		else if (stat == NetStat::Connected)
			return "SigMan::NetStat::Connected";
		else if (stat == NetStat::Handshaked)
			return "SigMan::NetStat::Handshaked";
		else if (stat == NetStat::Requested)
			return "SigMan::NetStat::Requested";
		else if (stat == NetStat::Got)
			return "SigMan::NetStat::Got";
		else if (stat == NetStat::PleaseClose)
			return "SigMan::NetStat::PleaseClose";
		else if (stat == NetStat::NetworkException)
			return "SigMan::NetStat::NetworkException";
		else if (stat == NetStat::CppGeneralException)
			return "SigMan::NetStat::CppGeneralException";
		return "UndefinedError";
	}
public:
	using SignalType = boost::signals2::signal<void(SigMan::NetStat)>;
	using SlotType = SignalType::slot_type;
	using ConnectionType = boost::signals2::connection;
private:
	SignalType sig;
public:
	ConnectionType connect(const SlotType & slot) {
		return sig.connect(slot);
	}
	void update(SigMan::NetStat status) {
		this->sig(status);
	}
};

class MessageTarget {
protected:
	SigMan::ConnectionType conn;
public:
	virtual void update(SigMan::NetStat status) = 0;
	void attach(SigMan & sigMan) {
		conn = sigMan.connect(
			std::bind(
				&MessageTarget::update,
				this,
				_1
			)
		);
	}
	void detach() {
		conn.disconnect();
	}
	virtual ~MessageTarget() {}
};

class PrintMessage: private MessageTarget {
public:
	PrintMessage(SigMan & sigMan) {
		this->attach(sigMan);
	}
	~PrintMessage() {
		this->detach();
	}
	void update(SigMan::NetStat status) override {
		std::cout << "SigMan::NetStat:" << std::endl;
		std::cout << '\t' << SigMan::NetStatusString(status) << std::endl;
	}
};

class TurnOffAlpha {
public:
	void operator()(irr::gui::IGUISkin * skin) {
		for (int i=0; i<irr::gui::EGDC_COUNT; ++i) {
			auto iter = (irr::gui::EGUI_DEFAULT_COLOR)i;
			irr::video::SColor color = skin->getColor(iter);
			color.setAlpha(0xff);
			skin->setColor(iter, color);
		}
	}
};

template <typename IrrlichtWindow>
class Event: public irr::IEventReceiver, private MessageTarget {
private:
	IrrlichtWindow & window;
private:
	irr::IrrlichtDevice * device;
	irr::video::IVideoDriver * driver;
	irr::scene::ISceneManager * smgr;
	irr::gui::IGUIEnvironment * igui;
	bool attached = false;
	std::future<void> sessionThread;
	std::atomic<bool> sessionRunning = false;

	irr::gui::IGUIEditBox * hostBox = nullptr;
	irr::gui::IGUIEditBox * portBox = nullptr;

	sf::Sound sound;
	sf::SoundBuffer soundBuffer;
	bool audioLoaded = false;
public:
	Event(
		IrrlichtWindow & window,
		SigMan & sigMan
	) noexcept
	:
		window{window},
		device{nullptr}
	{
		this->attach(sigMan);
		audioLoaded = soundBuffer.loadFromFile("./media/audio/click.ogg");
		if (audioLoaded)
			sound.setBuffer(soundBuffer);
	}
	~Event() {
		this->detach();
	}
	void attachDevice() {
		if (!window.opened)
			throw std::runtime_error{"Attach Event to Window Error: because window is not opened!"};
		device = window.device;
		device->setEventReceiver(this);
		attached = true;
		driver = device->getVideoDriver();
		smgr = device->getSceneManager();
		igui = device->getGUIEnvironment();
	}
	void update(SigMan::NetStat stat) override {
		if (stat == SigMan::NetStat::PleaseClose) {
			std::cout << "\"class Event\" Received PleaseClose:\n\t"
				<< SigMan::NetStatusString(stat)
				<< std::endl;
			sessionRunning = false;
			sessionThread = {};
		}
	}
	void playClick() {
		if (audioLoaded)
			sound.play();
	}
	bool OnEvent(const irr::SEvent & event) override {
		switch (event.EventType) {
		case irr::EET_KEY_INPUT_EVENT:
			this->processKey(event);
			break;
		case irr::EET_GUI_EVENT:
			this->processGUIEvent(event);
			break;
		default:
			break;
		}
		return false;
	}
	inline void processKey(const irr::SEvent & event) {
		if (event.KeyInput.PressedDown)
			return;
		switch (event.KeyInput.Key) {
		case irr::KEY_KEY_Q:
			window.opened = false;
			this->attached = false;
			device->closeDevice();
			std::cout << "MainWindow is closed!" << std::endl;
			break;
		default:
			break;
		}
	}
	inline void processGUIEvent(const irr::SEvent & event) {
		switch (event.GUIEvent.EventType) {
		case irr::gui::EGET_MENU_ITEM_SELECTED:
			this->playClick();
			this->processMenuItemSelected(event);
			break;
		case irr::gui::EGET_BUTTON_CLICKED:
			this->playClick();
			this->processButtonClicked(event);
			break;
		default:
			break;
		}
	}
	inline void processMenuItemSelected(const irr::SEvent & event) {
		auto * menu = static_cast<irr::gui::IGUIContextMenu *>(event.GUIEvent.Caller);
		irr::s32 id = menu->getItemCommandId(menu->getSelectedItem());
		switch (id) {
		case menuid_start:
			this->startNewSession();
			break;
		case menuid_about:
			igui->addMessageBox(
				L"About",
				L"Micburs - A cpp (c++) pogram to get Https(TLS) host resolve status. Window ui is written in irrlicht.\n\nLibraries:\n\t\t\t\tboost::asio\n\t\t\t\tboost::beast\n\t\t\t\tboost::signals2\n\t\t\t\tBotan::TLS\n\t\t\t\tSFML Audio\n\t\t\t\tIrrlicht Engine\n\t\t\t\t",
				true, // Modal
				irr::gui::EMBF_OK,
				nullptr, // parent
				-1, // id
				nullptr // texture
			);
			break;
		case menuid_quit:
			this->attached = false;
			window.opened = false;
			device->closeDevice();
			std::cout << "Window is closed!" << std::endl;
			break;
		default:
			break;
		}
	}
	inline void processButtonClicked(const irr::SEvent & event) {
		auto * button = static_cast<irr::gui::IGUIButton *>(event.GUIEvent.Caller);
		irr::s32 id = button->getID();
		switch (id) {
		case barid_start:
		case buttonid_start:
			std::cout << "Start New Session Received!" << std::endl;
			this->startNewSession();
			break;
		case barid_quit:
		case buttonid_quit:
			std::cout << "Quit Received!" << std::endl;
			this->attached = false;
			window.opened = false;
			device->closeDevice();
			break;
		default:
			break;
		}
	}
	void startNewSession() {
		bool isGood = true;
		std::wstring host;
		std::wstring port;
		const std::wstring title = L"Error Message Box:";
		std::wstring text;
		if (sessionRunning) {
			isGood = false;
			text = L"Please close the old session window! Old Session is Running!";
		} else {
			hostBox = window.hostBox;
			portBox = window.portBox;
			std::cout << "hotBox and portBox set!" << std::endl;
			host = hostBox->getText();
			port = portBox->getText();
			if (host == L"") {
				isGood = false;
				text = L"\"Host:\" area should not be empty!";
			} else if (port == L"") {
				port = L"443";
			}
		}

		if (!isGood) {
			igui->addMessageBox(
				title.data(),
				text.data(),
				true,
				irr::gui::EMBF_OK,
				nullptr,
				-1,
				nullptr
			);
			return;
		}
		std::string host_string;
		std::string port_string;
		std::copy(host.begin(), host.end(), std::back_inserter(host_string));
		std::copy(port.begin(), port.end(), std::back_inserter(port_string));
		std::cout << "Input (host, port) => " 
			<< "(" << host_string << ", " 
			<< port_string << ")"
			<< std::endl;
		sessionRunning = true;
		sessionThread = std::async(
			std::launch::async,
			&IrrlichtWindow::newSession,
			&window,
			host_string,
			port_string
		);
	}
};

class MainWindow {
private:	
	SigMan & sigMan;
	irr::u32 width;
	irr::u32 height;
	irr::video::E_DRIVER_TYPE driverType;
	irr::IrrlichtDevice * device;
	irr::scene::ISceneManager * smgr;
	irr::video::IVideoDriver * driver;
	irr::gui::IGUIEnvironment * igui;
	irr::io::IFileSystem * fs;
	std::future<void> windowThread;

	friend class Event<MainWindow>;
	class Event<MainWindow> event;

	bool opened;

	irr::gui::IGUIEditBox * hostBox = nullptr;
	irr::gui::IGUIEditBox * portBox = nullptr;
public:
	MainWindow(
		irr::u32 width,
		irr::u32 height,
		irr::video::E_DRIVER_TYPE driverType,
		SigMan & sigMan
	) noexcept
	:
		sigMan{sigMan},
		width{width>1280?width:1280},
		height{height>720?height:720},
		driverType{driverType},
		device{nullptr},
		event{std::ref(*this), sigMan},
		opened{false}
	{
		std::cout << "MainWindow: " << (this->width) << " x " << this->height
			<< std::endl;
	}
	~MainWindow() {
		windowThread.wait();
		device->drop();
		device = nullptr;
		opened = false;
		std::cout << "MainWindow Dropped!" << std::endl;
	}
public:
	void open() {
		device = irr::createDevice(
			driverType,
			irr::core::dimension2du{width, height},
			32,
			false,
			true,
			true,
			nullptr
		);
		if (device == nullptr)
			throw std::runtime_error{"MainWindow created failed!"};
		opened = true;
		event.attachDevice();
		this->startIrrlicht();
	}
private:
	void startIrrlicht() {
		device->setWindowCaption(L"Micburs NetStat Main Window - Powered by Irrlicht");
		device->setResizable(false);
		this->obtainIrrlichtObjects();
		this->setupFonts();
		this->createWidgets();
		this->startWindowLoop();
	}
	void obtainIrrlichtObjects() {
		smgr = device->getSceneManager();
		driver = device->getVideoDriver();
		igui = device->getGUIEnvironment();
		fs = device->getFileSystem();
	}
	void setupFonts() {
		fs->addFileArchive("./media");
		irr::gui::IGUISkin * skin = igui->getSkin();
		irr::gui::IGUIFont * font = igui->getFont("Liberation-Mono.1ASC.14-bold.xml");
		if (font == nullptr) {
			std::cerr << "Load Font Error!" << std::endl;
			return;
		} else {
			std::cout << "Load Font OK!" << std::endl;
			skin->setFont(font);
		}
		TurnOffAlpha{}(skin);
	}
	void createWidgets() {
	////////////////////////////////////////////////////////////////////////
		irr::gui::IGUIContextMenu * menu = igui->addMenu(
			nullptr,
			-1
		);
		irr::u32 sessionMenuId = menu->addItem(
			L"Session",
			-1, // Command ID
			true, // enabled
			true, // has sub menu
			false, // checked
			false // auto checking
		);
		irr::gui::IGUIContextMenu * sessionMenu = menu->getSubMenu(sessionMenuId);
		sessionMenu->addItem(
			L"Start Session ...",
			menuid_start,
			true,
			false,
			false,
			false
		);
	////////////////////////////////////////////////////////////////////////
		irr::u32 programMenuId = menu->addItem(
			L"Program",
			-1,
			true,
			true,
			false,
			false
		);
		irr::gui::IGUIContextMenu * programMenu = menu->getSubMenu(programMenuId);
		programMenu->addItem(
			L"About",
			menuid_about,
			true,
			false,
			false,
			false
		);
		programMenu->addItem(
			L"Quit Program ...",
			menuid_quit,
			true,
			false,
			false,
			false
		);
	////////////////////////////////////////////////////////////////////////
		irr::s32 x;
		irr::s32 y;

		const irr::s32 bar_w = 400;
		const irr::s32 bar_h = 80;

		irr::gui::IGUIToolBar * bar = igui->addToolBar(
			nullptr,
			-1
		);
		bar->setMinSize(irr::core::dimension2du{bar_w, bar_h});
		bar->setMaxSize(irr::core::dimension2du{bar_w, bar_h});
		bar->addButton(
			barid_start,
			L"Start",
			L"Start a boost::beast with Botan::TLS session ...",
			driver->getTexture("start64x64.png"), // Image: irr::video::ITexture *
			nullptr,  // Pressed Image: irr::video::ITexture*
			false, // is pressed down
			false // Use Alpha Channel?
		);
		bar->addButton(
			barid_quit,
			L"Quit",
			L"Exit Program ...",
			driver->getTexture("exit64x64.png"),
			nullptr,
			false,
			false
		);
	////////////////////////////////////////////////////////////////////////
		const irr::s32 w1 = 150;
		const irr::s32 h1 = 28;
		const irr::s32 x1 = (this->width - w1)/2;
		const irr::s32 y1 = (this->height)/8;

		x = x1;
		y = y1;

		igui->addButton(
			irr::core::recti{x, y, x+w1, y+h1},
			nullptr,
			buttonid_start,
			L"Start Session",
			L"Start a boost::beast with Botan::TLS session ..."
		);
	////////////////////////////////////////////////////////////////////////
		const irr::s32 w2 = this->width/4;
		const irr::s32 h2 = (irr::s32)(h1*0.75);
		const irr::s32 w2label = 40;
		x = (this->width - w2 - w2label-w2label/4)/2;
		y = y + h1 + h1/2;
		igui->addStaticText(
			L"Host:",
			irr::core::recti{x, y, x+w2label, y+h2},
			false, // border
			true, // world wrap
			nullptr, // parent
			-1, // id
			false // fill background
		);
		x = x+w2label+w2label/4;
		y = y;
		hostBox = igui->addEditBox(
			L"",
			irr::core::recti{x, y, x+w2, y+h2},
			true, // border
			nullptr, // parent
			-1
		);
		std::cout << "hostBox Created!" << std::endl;
	////////////////////////////////////////////////////////////////////////
		const irr::s32 w3 = w2;
		const irr::s32 h3 = h2;
		const irr::s32 w3label = w2label;
		x = x-w3label-w3label/4;
		y = y + h2 + h2/2;
		igui->addStaticText(
			L"Port:",
			irr::core::recti{x, y, x+w3label, y+h3},
			false,
			true,
			nullptr,
			-1,
			false
		);
		x = x + w3label + w3label/4;
		y = y;
		portBox = igui->addEditBox(
			L"443",
			irr::core::recti{x, y, x+w3, y+h3},
			true,
			nullptr,
			-1
		);
		std::cout << "portBox Created!" << std::endl;
	////////////////////////////////////////////////////////////////////////
		const irr::s32 img_w = 400;
		const irr::s32 img_h = 200;
		x = this->width - img_w - 10;
		y = this->height - img_h - 10;
		igui->addImage(
			driver->getTexture("leaf400x200.png"),
			irr::core::position2di{x, y},
			true, // Use Alpha Channel
			nullptr,
			-1,
			L"C++ Irrlicht"
		);
	////////////////////////////////////////////////////////////////////////
		const irr::s32 t_x = 320;
		const irr::s32 t_y = 320;
		x = (this->width - t_x)/2;
		y = (this->height - t_y)/2;
		igui->addStaticText(
			L"Input the hostname \nand the HTTPS(TLS) Port, \nthen Press \"Start Session\".\n\nHost can not be empty.\n\nPort can be empty.\n\nIf port is empty, the default TLS port number 443 will be used.\n\n(Cpp (c++) Project)\n\n",
			irr::core::recti{x, y, x + t_x, y + t_y},
			true, // border
			true, // world wrap
			nullptr, // parent
			-1, // id
			false // fill background
		);
	////////////////////////////////////////////////////////////////////////
		x = (this->width - w1)/2;
		y = this->height - h1 - h1/2;
		igui->addButton(
			irr::core::recti{x, y, x+w1, y+h1},
			nullptr,
			buttonid_quit,
			L"Quit Program",
			L"Quit This Program ..."
		);
	}
private:
	void startWindowLoop() {
		if (!opened)
			return;
		windowThread = std::async(
			std::launch::async,
				&MainWindow::runLoopInThread,
				this
		);
	}
private:
	void runLoopInThread() {
		while (device->run()) {
			smgr->drawAll();
			igui->drawAll();
			this->swapBuffersInThread();
		}
	}
	void swapBuffersInThread() {
		driver->endScene();
		driver->beginScene(
			irr::video::ECBF_COLOR | irr::video::ECBF_DEPTH,
			irr::video::SColor{std::to_underlying(Color::NLight)}
		);
	}
private:
	void newSession(const std::string & host, const std::string & port);
};

class OpenWindow: private MessageTarget {
private:
	irr::IrrlichtDevice * device = nullptr;
	bool opened = false;
	irr::scene::ISceneManager * smgr;
	irr::video::IVideoDriver * driver;
	irr::gui::IGUIEnvironment * igui;
	irr::io::IFileSystem * fs;
	std::future<void> windowThread;

	std::atomic<Color> color = Color::Grey;

	SigMan & circleSigMan;

	bool attached = false;

	sf::Music music;
public:
	OpenWindow(SigMan & sigMan)
	:
		circleSigMan{sigMan}
	{
		this->attach(sigMan);
		attached = true;
		if (music.openFromFile("./media/audio/session.ogg"))
			music.play();
	}
	~OpenWindow() {
		windowThread.wait();
		if (attached) {
			this->detach();
			attached = false;
		}
		music.stop();
		std::cout << "This session is closed!" << std::endl;
		circleSigMan.update(SigMan::NetStat::PleaseClose);
	}
	void update(SigMan::NetStat status) override {
		switch (status) {
		case SigMan::NetStat::ProgramStarted:
			this->openWindow();
			break;
		case SigMan::NetStat::Resolved:
			color = Color::LightBlue;
			break;
		case SigMan::NetStat::Connected:
			color = Color::DarkBlue;
			break;
		case SigMan::NetStat::Handshaked:
			color = Color::NGreen;
			break;
		case SigMan::NetStat::Requested:
			color = Color::NYellow;
			break;
		case SigMan::NetStat::Got:
			color = Color::NLight;
			break;
		case SigMan::NetStat::NetworkException:
			color = Color::Red1;
			break;
		case SigMan::NetStat::CppGeneralException:
			color = Color::Red2;
			break;
		default:
			break;
		}
	}
	void openWindow() {
		std::cout << "Trying to open a window ..." << std::endl;
		device = irr::createDevice(
			irr::video::EDT_BURNINGSVIDEO,
			irr::core::dimension2du{640, 360},
			32,
			false,
			true,
			true,
			nullptr
		);
		if (device != nullptr) {
			opened = true;
			this->obtainIrrlichtObjects();
		}
	}
	void obtainIrrlichtObjects() {
		device->setResizable(false);
		device->setWindowCaption(L"Micburs Session Window");
		smgr = device->getSceneManager();
		igui = device->getGUIEnvironment();
		driver = device->getVideoDriver();
		fs = device->getFileSystem();
		this->createWidgets();
	}
	void createWidgets() {
		fs->addFileArchive("./media");
		irr::gui::IGUISkin * skin = igui->getSkin();
		irr::gui::IGUIFont * font = igui->getFont("Liberation-Mono.1ASC.14-bold.xml");
		skin->setFont(font);
		std::wstring musicStatus;
		if (music.getStatus() == sf::Sound::Playing)
			musicStatus = L"Background Music Is Playing: true";
		else
			musicStatus = L"Background Music Is Playing: false";
		irr::gui::IGUIStaticText * infoText = igui->addStaticText(
			(
				std::wstring(L"Light Blue: Host Resolved\nDark Blue: Host Connected\nGreen: HTTPS(TLS) Handshaked\nYellow: Reuqest sent\nLight: Https Server Responsed (All Finished!)\nRed 1: Network Exception\nRed 2: Program Exception\n\n") + musicStatus
			).data(),
			irr::core::recti{20, 20, 500, 340},
			true,
			true,
			nullptr,
			-1,
			false
		);
		infoText->setOverrideColor(irr::video::SColor{0xff584552});
		windowThread = std::async(
			std::launch::async,
			&OpenWindow::startWindowLoop,
			this
		);
	}
	void startWindowLoop() {
		while (device->run()) {
			driver->beginScene(
				irr::video::ECBF_COLOR | irr::video::ECBF_DEPTH,
				irr::video::SColor{std::to_underlying(color.load())}
			);
			smgr->drawAll();
			igui->drawAll();
			driver->endScene();
		}
		device->drop();
		device = nullptr;
		if (attached) {
			this->detach();
			attached = false;
			std::cout << "Detaching boost::signals2 from session window ..."
				<< std::endl;
		}
		std::cout << "Session window closed, boost::signals2 detached!\n";
	}
};

class AppSession: public std::enable_shared_from_this<AppSession>, private MessageTarget {
private:
// Private members for boost::beast/asio session.
	const std::string host;
	const std::string port;
	asio::io_context ioContext;
	tcp::resolver resolver;
	std::shared_ptr<AppSession> self;
private:
// Private members for Botan::TLS session.
	CredentialsManager credMan;
	Botan::AutoSeeded_RNG rng;
	TLS::Session_Manager_In_Memory sessionMan;
	TLS::Policy policy;
	TLS::Server_Information serverInformation;
	TLS::Context tlsContext;
	TLS::Stream<beast::tcp_stream> tlsStream;
private:
// Private members for beast::http
	http::request<http::empty_body> req;
	http::response<http::string_body> res;
	beast::flat_buffer buffer;
private:
	SigMan & circleSigMan;
public:
	AppSession(
		const std::string & _host_,
		const std::string & _port_,
		SigMan & _sigMan_
	)
	:
		host{_host_},
		port{_port_},
		ioContext{},
		resolver{ioContext},
		credMan{},
		rng{},
		sessionMan{rng},
		policy{},
		serverInformation{host, port},
		tlsContext{credMan, rng, sessionMan, policy, serverInformation},
		tlsStream{tlsContext, ioContext},
		circleSigMan{_sigMan_}
	{
		this->attach(_sigMan_);
	}
	~AppSession() {
		this->detach();
	}
	// todo: Probably not working on "SigMan::NetStat::PleaseClose"
	void update(SigMan::NetStat stat) override {
		if (stat == SigMan::NetStat::PleaseClose) {
			std::cout << "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV\n";
			std::cout << "\"class AppSession\" Received PleaseClose:\n\t"
				<< SigMan::NetStatusString(stat)
				<< std::endl;
		} else {
			std::cout << "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n";
			std::cout << "Other Message:\n\t"
				<< SigMan::NetStatusString(stat) << std::endl;
		}
	}
	void start() {
		try {
			self = this->shared_from_this();
			self->resolve();
			ioContext.run();
			std::cout << "************************************************************************\n";
			std::cout << "Network Session Closed!\n";
		} catch (std::exception & exc) {
			std::cerr << "[Network Exception]" << exc.what() << std::endl;
			circleSigMan.update(SigMan::NetStat::NetworkException);
		}
	}
	void resolve() {
		resolver.async_resolve(
			host,
			port,
			[self=self] (
				beast::error_code ec,
				tcp::resolver::results_type results
			) {
				if (ec)
					throw std::system_error{ec, "Resolve Error"};
				self->circleSigMan.update(SigMan::NetStat::Resolved);
				self->connect(std::move(results));
			}
		);
	}
	void connect(tcp::resolver::results_type && results) {
		tlsStream.next_layer().expires_after(std::chrono::seconds(12));
		tlsStream.next_layer().async_connect(
			results,
			[self=self] (
				beast::error_code ec,
				tcp::endpoint ep
			) {
				if (ec)
					throw std::system_error{ec, "Connect Error"};
				self->circleSigMan.update(SigMan::NetStat::Connected);
				self->handshake();
			}
		);
	}
	void handshake() {
		tlsStream.next_layer().expires_after(std::chrono::seconds(12));
		tlsStream.async_handshake(
			TLS::Connection_Side::CLIENT,
			[self=self] (
				beast::error_code ec
			) {
				if (ec)
					throw std::system_error{ec, "Handshake Error"};
				self->circleSigMan.update(SigMan::NetStat::Handshaked);
				self->write();
			}
		);
	}
	void write() {
		req.method(http::verb::get);
		req.version(11);
		req.target("/");
		req.set(http::field::host, host);
		req.set(http::field::user_agent, "Botan::TLS-boost::beast-Session-"s + BOOST_BEAST_VERSION_STRING);
		tlsStream.next_layer().expires_after(std::chrono::seconds(12));
		http::async_write(
			tlsStream,
			req,
			[self=self] (
				beast::error_code ec,
				std::size_t size
			) {
				if (ec)
					throw std::system_error{ec, "Http Request Error"};
				self->circleSigMan.update(SigMan::NetStat::Requested);
				self->read();
			}
		);
	}
	void read() {
		tlsStream.next_layer().expires_after(std::chrono::seconds(12));
		http::async_read(
			tlsStream,
			buffer,
			res,
			[self=self] (
				beast::error_code ec,
				std::size_t size
			) {
				if (ec)
					throw std::system_error{ec, "Read Web Content Error"};
				self->circleSigMan.update(SigMan::NetStat::Got);
				std::cout << "------------------------------------------------------------------------" << std::endl;
				std::cout << self->res << std::endl;
			}
		);
	}
};

auto startSession = [] (
	const std::string & host,
	const std::string & port,
	SigMan & sigMan
) {
	try {
		sigMan.update(SigMan::NetStat::ProgramStarted);

		std::cout << "Hello, Cpp! The c++ programming language." << std::endl;
		auto appSession = std::make_shared<AppSession>(host, port, sigMan);

		appSession->start();
	} catch (std::exception & exc) {
		sigMan.update(SigMan::NetStat::CppGeneralException);
		std::cerr << "[Cpp General Exception]" << exc.what() << std::endl;
	}
};

auto startMainWindow = [] (irr::video::E_DRIVER_TYPE driverType, SigMan & sigMan) {
	try {
		MainWindow mainWindow{1234, 694, driverType, sigMan};
		mainWindow.open();
	} catch (std::exception & exc) {
		std::cerr << "[Irrlicht Exception]" << std::endl;
	}
};

void MainWindow::newSession(const std::string & host, const std::string & port) {
	OpenWindow openWindow{this->sigMan};
	std::cout << "A new session is started!\n";
	auto sessionThread = std::async(
		std::launch::async,
		startSession,
		host,
		port,
		std::ref(this->sigMan)
	);
}

int main() try {
	SigMan sigMan;
	PrintMessage printMessage{sigMan};
	const irr::video::E_DRIVER_TYPE driverType = irr::video::EDT_BURNINGSVIDEO;
	auto mainWindowThread = std::async(
		std::launch::async,
		startMainWindow,
		driverType,
		std::ref(sigMan)
	);
} catch (...) {
	std::cerr << "[Cpp Unknown Exception]" << std::endl;
	return 2;
}

