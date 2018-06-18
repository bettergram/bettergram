/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_text.h"

#include "export/data/export_data_types.h"
#include "core/utils.h"

#include <QtCore/QFile>

namespace Export {
namespace Output {
namespace {

#ifdef Q_OS_WIN
const auto kLineBreak = QByteArrayLiteral("\r\n");
#else // Q_OS_WIN
const auto kLineBreak = QByteArrayLiteral("\n");
#endif // Q_OS_WIN

void SerializeMultiline(
		QByteArray &appendTo,
		const QByteArray &value,
		int newline) {
	const auto data = value.data();
	auto offset = 0;
	do {
		appendTo.append("> ");
		const auto win = (newline > 0 && *(data + newline - 1) == '\r');
		if (win) --newline;
		appendTo.append(data + offset, newline - offset).append(kLineBreak);
		if (win) ++newline;
		offset = newline + 1;
		newline = value.indexOf('\n', offset);
	} while (newline > 0);
	if (const auto size = value.size(); size > offset) {
		appendTo.append("> ");
		appendTo.append(data + offset, size - offset).append(kLineBreak);
	}
}

QByteArray JoinList(
		const QByteArray &separator,
		const std::vector<QByteArray> &list) {
	if (list.empty()) {
		return QByteArray();
	} else if (list.size() == 1) {
		return list[0];
	}
	auto size = (list.size() - 1) * separator.size();
	for (const auto &value : list) {
		size += value.size();
	}
	auto result = QByteArray();
	result.reserve(size);
	auto counter = 0;
	while (true) {
		result.append(list[counter]);
		if (++counter == list.size()) {
			break;
		} else {
			result.append(separator);
		}
	}
	return result;
}

QByteArray SerializeKeyValue(
		std::vector<std::pair<QByteArray, QByteArray>> &&values) {
	auto result = QByteArray();
	for (const auto &[key, value] : values) {
		if (value.isEmpty()) {
			continue;
		}
		result.append(key);
		if (const auto newline = value.indexOf('\n'); newline >= 0) {
			result.append(':').append(kLineBreak);
			SerializeMultiline(result, value, newline);
		} else {
			result.append(": ").append(value).append(kLineBreak);
		}
	}
	return result;
}

Data::Utf8String FormatUsername(const Data::Utf8String &username) {
	return username.isEmpty() ? username : ('@' + username);
}

QByteArray FormatFilePath(const Data::File &file) {
	return file.relativePath.toUtf8();
}

QByteArray SerializeMessage(
		const Data::Message &message,
		const std::map<Data::PeerId, Data::Peer> &peers,
		const QString &internalLinksDomain) {
	using namespace Data;

	if (message.media.content.is<UnsupportedMedia>()) {
		return "Error! This message is not supported "
			"by this version of Telegram Desktop. "
			"Please update the application.";
	}

	const auto peer = [&](PeerId peerId) -> const Peer& {
		if (const auto i = peers.find(peerId); i != end(peers)) {
			return i->second;
		}
		static auto empty = Peer{ User() };
		return empty;
	};
	const auto user = [&](int32 userId) -> const User& {
		if (const auto result = peer(UserPeerId(userId)).user()) {
			return *result;
		}
		static auto empty = User();
		return empty;
	};
	const auto chat = [&](int32 chatId) -> const Chat& {
		if (const auto result = peer(ChatPeerId(chatId)).chat()) {
			return *result;
		}
		static auto empty = Chat();
		return empty;
	};

	auto values = std::vector<std::pair<QByteArray, QByteArray>>{
		{ "ID", NumberToString(message.id) },
		{ "Date", FormatDateTime(message.date) },
		{ "Edited", FormatDateTime(message.edited) },
	};
	const auto push = [&](const QByteArray &key, const QByteArray &value) {
		if (!value.isEmpty()) {
			values.emplace_back(key, value);
		}
	};
	const auto wrapPeerName = [&](PeerId peerId) {
		const auto result = peer(peerId).name();
		return result.isEmpty() ? QByteArray("(unknown peer)") : result;
	};
	const auto wrapUserName = [&](int32 userId) {
		const auto result = user(userId).name();
		return result.isEmpty() ? QByteArray("(unknown user)") : result;
	};
	const auto pushFrom = [&](const QByteArray &label = "From") {
		if (message.fromId) {
			push(label, wrapUserName(message.fromId));
		}
	};
	const auto pushReplyToMsgId = [&](
			const QByteArray &label = "Reply to message") {
		if (message.replyToMsgId) {
			push(label, "ID-" + NumberToString(message.replyToMsgId));
		}
	};
	const auto pushUserNames = [&](
			const std::vector<int32> &data,
			const QByteArray &labelOne = "Member",
			const QByteArray &labelMany = "Members") {
		auto list = std::vector<QByteArray>();
		for (const auto userId : data) {
			list.push_back(wrapUserName(userId));
		}
		if (list.size() == 1) {
			push(labelOne, list[0]);
		} else if (!list.empty()) {
			push(labelMany, JoinList(", ", list));
		}
	};
	const auto pushActor = [&] {
		pushFrom("Actor");
	};
	const auto pushAction = [&](const QByteArray &action) {
		push("Action", action);
	};
	const auto pushTTL = [&](
		const QByteArray &label = "Self destruct period") {
		if (const auto ttl = message.media.ttl) {
			push(label, NumberToString(ttl) + " sec.");
		}
	};

	using SkipReason = Data::File::SkipReason;
	const auto pushPath = [&](
			const Data::File &file,
			const QByteArray &label,
			const QByteArray &name = QByteArray()) {
		Expects(!file.relativePath.isEmpty()
			|| file.skipReason != SkipReason::None);

		push(label, [&]() -> QByteArray {
			const auto pre = name.isEmpty() ? QByteArray() : name + ' ';
			switch (file.skipReason) {
			case SkipReason::Unavailable: return pre + "(file unavailable)";
			case SkipReason::FileSize: return pre + "(file too large)";
			case SkipReason::FileType: return pre + "(file skipped)";
			case SkipReason::None: return FormatFilePath(file);
			}
			Unexpected("Skip reason while writing file path.");
		}());
	};
	const auto pushPhoto = [&](const Image &image) {
		pushPath(image.file, "Photo");
		if (image.width && image.height) {
			push("Width", NumberToString(image.width));
			push("Height", NumberToString(image.height));
		}
	};

	message.action.content.match([&](const ActionChatCreate &data) {
		pushActor();
		pushAction("Create group");
		push("Title", data.title);
		pushUserNames(data.userIds);
	}, [&](const ActionChatEditTitle &data) {
		pushActor();
		pushAction("Edit group title");
		push("New title", data.title);
	}, [&](const ActionChatEditPhoto &data) {
		pushActor();
		pushAction("Edit group photo");
		pushPhoto(data.photo.image);
	}, [&](const ActionChatDeletePhoto &data) {
		pushActor();
		pushAction("Delete group photo");
	}, [&](const ActionChatAddUser &data) {
		pushActor();
		pushAction("Invite members");
		pushUserNames(data.userIds);
	}, [&](const ActionChatDeleteUser &data) {
		pushActor();
		pushAction("Remove members");
		push("Member", wrapUserName(data.userId));
	}, [&](const ActionChatJoinedByLink &data) {
		pushActor();
		pushAction("Join group by link");
		push("Inviter", wrapUserName(data.inviterId));
	}, [&](const ActionChannelCreate &data) {
		pushActor();
		pushAction("Create channel");
		push("Title", data.title);
	}, [&](const ActionChatMigrateTo &data) {
		pushActor();
		pushAction("Migrate this group to supergroup");
	}, [&](const ActionChannelMigrateFrom &data) {
		pushActor();
		pushAction("Migrate this supergroup from group");
		push("Title", data.title);
	}, [&](const ActionPinMessage &data) {
		pushActor();
		pushAction("Pin message");
		pushReplyToMsgId("Message");
	}, [&](const ActionHistoryClear &data) {
		pushActor();
		pushAction("Clear history");
	}, [&](const ActionGameScore &data) {
		pushActor();
		pushAction("Score in a game");
		pushReplyToMsgId("Game message");
		push("Score", NumberToString(data.score));
	}, [&](const ActionPaymentSent &data) {
		pushAction("Send payment");
		push(
			"Amount",
			Data::FormatMoneyAmount(data.amount, data.currency));
		pushReplyToMsgId("Invoice message");
	}, [&](const ActionPhoneCall &data) {
		pushActor();
		pushAction("Phone call");
		if (data.duration) {
			push("Duration", NumberToString(data.duration) + " sec.");
		}
		using Reason = ActionPhoneCall::DiscardReason;
		push("Discard reason", [&] {
			switch (data.discardReason) {
			case Reason::Busy: return "Busy";
			case Reason::Disconnect: return "Disconnect";
			case Reason::Hangup: return "Hangup";
			case Reason::Missed: return "Missed";
			}
			return "";
		}());
	}, [&](const ActionScreenshotTaken &data) {
		pushActor();
		pushAction("Take screenshot");
	}, [&](const ActionCustomAction &data) {
		pushActor();
		push("Information", data.message);
	}, [&](const ActionBotAllowed &data) {
		pushAction("Allow sending messages");
		push("Reason", "Login on \"" + data.domain + "\"");
	}, [&](const ActionSecureValuesSent &data) {
		pushAction("Send Telegram Passport values");
		auto list = std::vector<QByteArray>();
		for (const auto type : data.types) {
			list.push_back([&] {
				using Type = ActionSecureValuesSent::Type;
				switch (type) {
				case Type::PersonalDetails: return "Personal details";
				case Type::Passport: return "Passport";
				case Type::DriverLicense: return "Driver license";
				case Type::IdentityCard: return "Identity card";
				case Type::InternalPassport: return "Internal passport";
				case Type::Address: return "Address information";
				case Type::UtilityBill: return "Utility bill";
				case Type::BankStatement: return "Bank statement";
				case Type::RentalAgreement: return "Rental agreement";
				case Type::PassportRegistration:
					return "Passport registration";
				case Type::TemporaryRegistration:
					return "Temporary registration";
				case Type::Phone: return "Phone number";
				case Type::Email: return "Email";
				}
				return "";
			}());
		}
		if (list.size() == 1) {
			push("Value", list[0]);
		} else if (!list.empty()) {
			push("Values", JoinList(", ", list));
		}
	}, [](const base::none_type &) {});

	if (!message.action.content) {
		pushFrom();
		push("Author", message.signature);
		if (message.forwardedFromId) {
			push("Forwarded from", wrapPeerName(message.forwardedFromId));
		}
		pushReplyToMsgId();
		if (message.viaBotId) {
			push("Via", user(message.viaBotId).username);
		}
	}

	message.media.content.match([&](const Photo &photo) {
		pushPhoto(photo.image);
		pushTTL();
	}, [&](const Document &data) {
		const auto pushMyPath = [&](const QByteArray &label) {
			return pushPath(data.file, label);
		};
		if (data.isSticker) {
			pushMyPath("Sticker");
			push("Emoji", data.stickerEmoji);
		} else if (data.isVideoMessage) {
			pushMyPath("Video message");
		} else if (data.isVoiceMessage) {
			pushMyPath("Voice message");
		} else if (data.isAnimated) {
			pushMyPath("Animation");
		} else if (data.isVideoFile) {
			pushMyPath("Video file");
		} else if (data.isAudioFile) {
			pushMyPath("Audio file");
			push("Performer", data.songPerformer);
			push("Title", data.songTitle);
		} else {
			pushMyPath("File");
		}
		if (!data.isSticker) {
			push("Mime type", data.mime);
		}
		if (data.duration) {
			push("Duration", NumberToString(data.duration) + " sec.");
		}
		if (data.width && data.height) {
			push("Width", NumberToString(data.width));
			push("Height", NumberToString(data.height));
		}
		pushTTL();
	}, [&](const ContactInfo &data) {
		push("Contact information", SerializeKeyValue({
			{ "First name", data.firstName },
			{ "Last name", data.lastName },
			{ "Phone number", FormatPhoneNumber(data.phoneNumber) },
		}));
	}, [&](const GeoPoint &data) {
		push("Location", data.valid ? SerializeKeyValue({
			{ "Latitude", NumberToString(data.latitude) },
			{ "Longitude", NumberToString(data.longitude) },
		}) : QByteArray("(empty value)"));
		pushTTL("Live location period");
	}, [&](const Venue &data) {
		push("Place name", data.title);
		push("Address", data.address);
		if (data.point.valid) {
			push("Location", SerializeKeyValue({
				{ "Latitude", NumberToString(data.point.latitude) },
				{ "Longitude", NumberToString(data.point.longitude) },
			}));
		}
	}, [&](const Game &data) {
		push("Game", data.title);
		push("Description", data.description);
		if (data.botId != 0 && !data.shortName.isEmpty()) {
			const auto bot = user(data.botId);
			if (bot.isBot && !bot.username.isEmpty()) {
				push("Link", internalLinksDomain.toUtf8()
					+ bot.username
					+ "?game="
					+ data.shortName);
			}
		}
	}, [&](const Invoice &data) {
		push("Invoice", SerializeKeyValue({
			{ "Title", data.title },
			{ "Description", data.description },
			{
				"Amount",
				Data::FormatMoneyAmount(data.amount, data.currency)
			},
			{ "Receipt message", (data.receiptMsgId
				? "ID-" + NumberToString(data.receiptMsgId)
				: QByteArray()) }
		}));
	}, [](const UnsupportedMedia &data) {
		Unexpected("Unsupported message.");
	}, [](const base::none_type &) {});

	push("Text", message.text);

	return SerializeKeyValue(std::move(values));
}

} // namespace

bool TextWriter::start(const Settings &settings) {
	Expects(settings.path.endsWith('/'));

	_settings = base::duplicate(settings);
	_result = fileWithRelativePath(mainFileRelativePath());
	return true;
}

bool TextWriter::writePersonal(const Data::PersonalInfo &data) {
	Expects(_result != nullptr);

	const auto &info = data.user.info;
	const auto serialized = "Personal information"
		+ kLineBreak
		+ kLineBreak
		+ SerializeKeyValue({
		{ "First name", info.firstName },
		{ "Last name", info.lastName },
		{ "Phone number", Data::FormatPhoneNumber(info.phoneNumber) },
		{ "Username", FormatUsername(data.user.username) },
		{ "Bio", data.bio },
		})
		+ kLineBreak;
	return _result->writeBlock(serialized) == File::Result::Success;
}

bool TextWriter::writeUserpicsStart(const Data::UserpicsInfo &data) {
	Expects(_result != nullptr);

	_userpicsCount = data.count;
	if (!_userpicsCount) {
		return true;
	}
	const auto serialized = "Personal photos "
		"(" + Data::NumberToString(_userpicsCount) + ")"
		+ kLineBreak
		+ kLineBreak;
	return _result->writeBlock(serialized) == File::Result::Success;
}

bool TextWriter::writeUserpicsSlice(const Data::UserpicsSlice &data) {
	Expects(_result != nullptr);
	Expects(!data.list.empty());

	auto lines = QByteArray();
	for (const auto &userpic : data.list) {
		if (!userpic.date) {
			lines.append("(deleted photo)");
		} else {
			lines.append(Data::FormatDateTime(userpic.date)).append(" - ");
			if (userpic.image.file.relativePath.isEmpty()) {
				lines.append("(file unavailable)");
			} else {
				lines.append(userpic.image.file.relativePath.toUtf8());
			}
		}
		lines.append(kLineBreak);
	}
	return _result->writeBlock(lines) == File::Result::Success;
}

bool TextWriter::writeUserpicsEnd() {
	Expects(_result != nullptr);

	return (_userpicsCount > 0)
		? _result->writeBlock(kLineBreak) == File::Result::Success
		: true;
}

bool TextWriter::writeContactsList(const Data::ContactsList &data) {
	Expects(_result != nullptr);

	if (data.list.empty()) {
		return true;
	}

	const auto file = fileWithRelativePath("contacts.txt");
	auto list = std::vector<QByteArray>();
	list.reserve(data.list.size());
	for (const auto &index : Data::SortedContactsIndices(data)) {
		const auto &contact = data.list[index];
		if (contact.firstName.isEmpty()
			&& contact.lastName.isEmpty()
			&& contact.phoneNumber.isEmpty()) {
			list.push_back("(deleted user)" + kLineBreak);
		} else {
			list.push_back(SerializeKeyValue({
				{ "First name", contact.firstName },
				{ "Last name", contact.lastName },
				{
					"Phone number",
					Data::FormatPhoneNumber(contact.phoneNumber)
				},
				{ "Date", Data::FormatDateTime(contact.date) }
			}));
		}
	}
	const auto full = JoinList(kLineBreak, list);
	if (file->writeBlock(full) != File::Result::Success) {
		return false;
	}

	const auto header = "Contacts "
		"(" + Data::NumberToString(data.list.size()) + ") - contacts.txt"
		+ kLineBreak
		+ kLineBreak;
	return _result->writeBlock(header) == File::Result::Success;
}

bool TextWriter::writeSessionsList(const Data::SessionsList &data) {
	Expects(_result != nullptr);

	if (data.list.empty()) {
		return true;
	}

	const auto file = fileWithRelativePath("sessions.txt");
	auto list = std::vector<QByteArray>();
	list.reserve(data.list.size());
	for (const auto &session : data.list) {
		list.push_back(SerializeKeyValue({
			{ "Last active", Data::FormatDateTime(session.lastActive) },
			{ "Last IP address", session.ip },
			{ "Last country", session.country },
			{ "Last region", session.region },
			{
				"Application name",
				(session.applicationName.isEmpty()
					? Data::Utf8String("(unknown)")
					: session.applicationName)
			},
			{ "Application version", session.applicationVersion },
			{ "Device model", session.deviceModel },
			{ "Platform", session.platform },
			{ "System version", session.systemVersion },
			{ "Created", Data::FormatDateTime(session.created) },
		}));
	}
	const auto full = JoinList(kLineBreak, list);
	if (file->writeBlock(full) != File::Result::Success) {
		return false;
	}

	const auto header = "Sessions "
		"(" + Data::NumberToString(data.list.size()) + ") - sessions.txt"
		+ kLineBreak
		+ kLineBreak;
	return _result->writeBlock(header) == File::Result::Success;
}

bool TextWriter::writeDialogsStart(const Data::DialogsInfo &data) {
	return writeChatsStart(data, "Chats", "chats.txt");
}

bool TextWriter::writeDialogStart(const Data::DialogInfo &data) {
	return writeChatStart(data);
}

bool TextWriter::writeDialogSlice(const Data::MessagesSlice &data) {
	return writeChatSlice(data);
}

bool TextWriter::writeDialogEnd() {
	return writeChatEnd();
}

bool TextWriter::writeDialogsEnd() {
	return true;
}

bool TextWriter::writeLeftChannelsStart(const Data::DialogsInfo &data) {
	return writeChatsStart(data, "Left chats", "left_chats.txt");
}

bool TextWriter::writeLeftChannelStart(const Data::DialogInfo &data) {
	return writeChatStart(data);
}

bool TextWriter::writeLeftChannelSlice(const Data::MessagesSlice &data) {
	return writeChatSlice(data);
}

bool TextWriter::writeLeftChannelEnd() {
	return writeChatEnd();
}

bool TextWriter::writeLeftChannelsEnd() {
	return true;
}

bool TextWriter::writeChatsStart(
		const Data::DialogsInfo &data,
		const QByteArray &listName,
		const QString &fileName) {
	Expects(_result != nullptr);

	if (data.list.empty()) {
		return true;
	}

	_dialogsCount = data.list.size();

	using Type = Data::DialogInfo::Type;
	const auto TypeString = [](Type type) {
		switch (type) {
		case Type::Unknown: return "(unknown)";
		case Type::Personal: return "Personal chat";
		case Type::Bot: return "Bot chat";
		case Type::PrivateGroup: return "Private group";
		case Type::PublicGroup: return "Public group";
		case Type::PrivateChannel: return "Private channel";
		case Type::PublicChannel: return "Private channel";
		}
		Unexpected("Dialog type in TypeString.");
	};
	const auto NameString = [](
			const Data::Utf8String &name,
			Type type) -> QByteArray {
		if (!name.isEmpty()) {
			return name;
		}
		switch (type) {
		case Type::Unknown: return "(unknown)";
		case Type::Personal: return "(deleted user)";
		case Type::Bot: return "(deleted bot)";
		case Type::PrivateGroup:
		case Type::PublicGroup: return "(deleted group)";
		case Type::PrivateChannel:
		case Type::PublicChannel: return "(deleted channel)";
		}
		Unexpected("Dialog type in TypeString.");
	};
	const auto file = fileWithRelativePath(fileName);
	auto list = std::vector<QByteArray>();
	list.reserve(data.list.size());
	auto index = 0;
	for (const auto &dialog : data.list) {
		const auto path = dialog.relativePath + "messages.txt";
		list.push_back(SerializeKeyValue({
			{ "Name", NameString(dialog.name, dialog.type) },
			{ "Type", TypeString(dialog.type) },
			{ "Content", path.toUtf8() }
		}));
	}
	const auto full = JoinList(kLineBreak, list);
	if (file->writeBlock(full) != File::Result::Success) {
		return false;
	}

	const auto header = listName + " "
		"(" + Data::NumberToString(data.list.size()) + ") - "
		+ fileName.toUtf8()
		+ kLineBreak
		+ kLineBreak;
	return _result->writeBlock(header) == File::Result::Success;
}

bool TextWriter::writeChatStart(const Data::DialogInfo &data) {
	Expects(_chat == nullptr);
	Expects(_dialogIndex < _dialogsCount);

	const auto digits = Data::NumberToString(_dialogsCount - 1).size();
	const auto number = Data::NumberToString(++_dialogIndex, digits, '0');
	_chat = fileWithRelativePath(data.relativePath + "messages.txt");
	_dialogEmpty = true;
	_dialogOnlyMy = data.onlyMyMessages;
	return true;
}

bool TextWriter::writeChatSlice(const Data::MessagesSlice &data) {
	Expects(_chat != nullptr);
	Expects(!data.list.empty());

	_dialogEmpty = false;
	auto list = std::vector<QByteArray>();
	list.reserve(data.list.size());
	auto index = 0;
	for (const auto &message : data.list) {
		list.push_back(SerializeMessage(
			message,
			data.peers,
			_settings.internalLinksDomain));
	}
	const auto full = _chat->empty()
		? JoinList(kLineBreak, list)
		: kLineBreak + JoinList(kLineBreak, list);
	return _chat->writeBlock(full) == File::Result::Success;
}

bool TextWriter::writeChatEnd() {
	Expects(_chat != nullptr);

	if (_dialogEmpty) {
		_chat->writeBlock(_dialogOnlyMy
			? "No outgoing messages in this chat."
			: "No messages in this chat.");
	}
	_chat = nullptr;
	return true;
}

bool TextWriter::finish() {
	return true;
}

QString TextWriter::mainFilePath() {
	return pathWithRelativePath(mainFileRelativePath());
}

QString TextWriter::mainFileRelativePath() const {
	return "result.txt";
}

QString TextWriter::pathWithRelativePath(const QString &path) const {
	return _settings.path + path;
}

std::unique_ptr<File> TextWriter::fileWithRelativePath(
		const QString &path) const {
	return std::make_unique<File>(pathWithRelativePath(path));
}

} // namespace Output
} // namespace Export