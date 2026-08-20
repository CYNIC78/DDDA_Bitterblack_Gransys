#include "stdafx.h"
#include "iniConfig.h"

iniConfig::iniConfig(LPCSTR fileName)
{
	this->fileName = fileName;

	SetLastError(ERROR_SUCCESS);
	GetPrivateProfileSectionNamesA(buffer, sizeof buffer, fileName);
	if (GetLastError() == ERROR_FILE_NOT_FOUND)
		logFile << "Config: file not found!" << std::endl;
}

bool iniConfig::get(LPCSTR section, LPCSTR key, bool allowEmpty)
{
	SetLastError(ERROR_SUCCESS);
	DWORD result = GetPrivateProfileStringA(section, key, nullptr, buffer, sizeof buffer, fileName);
	return GetLastError() != ERROR_FILE_NOT_FOUND && (allowEmpty || result > 0);
}

void iniConfig::removeKey(LPCSTR section, LPCSTR key) const { WritePrivateProfileStringA(section, key, nullptr, fileName); }
std::vector<int> iniConfig::getSectionInts(LPCSTR section)
{
	std::vector<int> keys;
	if (get(section, nullptr, false))
	{
		LPSTR nextKey = buffer;
		while (*nextKey != '\0')
		{
			try { keys.push_back(std::stoi(nextKey)); }
			catch (...) {}
			nextKey = nextKey + strlen(nextKey) + 1;
		}
	}
	return keys;
}

// Build 69.4: лог засорялся сотнями строк «has invalid value» на КАЖДЫЙ
// отсутствующий ключ. Отсутствие ключа — норма: почти все настройки
// необязательны и берут значение по умолчанию. Ругаться нужно только тогда,
// когда ключ В ФАЙЛЕ ЕСТЬ, но значение не разбирается, — вот это ошибка
// пользователя, о которой стоит сказать. И сказать один раз, а не каждый
// раз, когда конфиг перечитывается по mtime.
static bool reportOnce(LPCSTR section, LPCSTR key)
{
	static std::vector<string> seen;
	string id = string(section ? section : "?") + "->" + string(key ? key : "?");
	for (size_t i = 0; i < seen.size(); ++i)
		if (seen[i] == id) return false;
	seen.push_back(id);
	return true;
}

template <typename T>
T printError(LPCSTR section, LPCSTR key, T defValue)
{
	if (reportOnce(section, key))
		logFile << "Config: " << section << "->" << key
		        << " has invalid value, using default (" << defValue << ")" << std::endl;
	return defValue;
}


// ================= ДОСЫЛКА НЕДОСТАЮЩИХ КЛЮЧЕЙ (75.46) =================
//
// БОЛЬ, КОТОРУЮ ЭТО ЗАКРЫВАЕТ (тестер, 20.08):
//
//   «Прикинь, мне каждый новый тест выставлять настройки заново, когда
//    обновляется инишник в билде.»
//
// Причина была не в чтении: отсутствующий ключ и так молча берёт значение
// по умолчанию, старый ini работает с новой сборкой. Беда в том, что вместе
// со сборкой ехал ГОТОВЫЙ ini, и, положив его в игру, тестер затирал свои
// значения чужими умолчаниями.
//
// Лечим с двух сторон:
//   1) сборка больше не везёт `ddda_ai_overhaul.ini`. Она везёт
//      `ddda_ai_overhaul.default.ini` — справочник, который ничего не
//      затирает, потому что называется иначе;
//   2) мод сам ДОПИСЫВАЕТ в пользовательский ini ключи, которых там нет,
//      с их значениями по умолчанию. Ключ появился в новой версии — он
//      сам возникнет в файле, а всё, что тестер уже выставил, останется
//      нетронутым.
//
// Дописываем ровно один раз на ключ: после записи он в файле есть, и
// следующее чтение идёт обычным путём. Списки (`getInts`/`getFloats`) не
// трогаем — у них нет осмысленного «значения по умолчанию», которое можно
// записать текстом.
bool iniConfig::fileExists() const
{
	return GetFileAttributesA(fileName) != INVALID_FILE_ATTRIBUTES;
}

void iniConfig::backfill(LPCSTR section, LPCSTR key, LPCSTR value) const
{
	if (!autoBackfill || !section || !key || !value) return;
	// Файла нет вовсе — свежая установка. Создаём с шапкой, дальше он
	// наполнится сам: каждый ключ допишется при первом же чтении.
	if (!fileExists()) {
		std::ofstream f(fileName);
		if (!f) return;
		f << "; DDDA AI Overhaul - settings\n"
		     "; This file is written by the mod. Missing keys are added with\n"
		     "; their defaults; values you set here are never overwritten.\n"
		     "; Full reference with comments: ddda_ai_overhaul.default.ini\n";
		f.close();
		logFile << "Config: created " << fileName
		        << " (fresh install; defaults will be filled in)" << std::endl;
	}
	WritePrivateProfileStringA(section, key, value, fileName);
	logFile << "Config: added missing key [" << section << "] " << key
	        << " = " << value << " (new option, default written)" << std::endl;
}

string iniConfig::getStr(LPCSTR section, LPCSTR key, string defValue)
{
	if (get(section, key, defValue.empty()))
		return buffer;
	return defValue;   // ключа нет — это норма, молчим
}

int iniConfig::getInt(LPCSTR section, LPCSTR key, int defValue)
{
	if (!get(section, key)) { char t[32]; sprintf_s(t, "%d", defValue); backfill(section, key, t); return defValue; }
	try { return std::stoi(buffer, nullptr, 0); }
	catch (...) {}
	return printError(section, key, defValue); // ключ есть, но значение битое
}

unsigned int iniConfig::getUInt(LPCSTR section, LPCSTR key, unsigned defValue)
{
	if (!get(section, key)) { char t[32]; sprintf_s(t, "%u", defValue); backfill(section, key, t); return defValue; }
	try { return std::stoul(buffer, nullptr, 0); }
	catch (...) {}
	return printError(section, key, defValue);
}

float iniConfig::getFloat(LPCSTR section, LPCSTR key, float defValue)
{
	if (!get(section, key)) { char t[32]; sprintf_s(t, "%g", defValue); backfill(section, key, t); return defValue; }
	try { return std::stof(buffer, nullptr); }
	catch (...) {}
	return printError(section, key, defValue);
}

double iniConfig::getDouble(LPCSTR section, LPCSTR key, double defValue)
{
	if (!get(section, key)) { char t[32]; sprintf_s(t, "%g", defValue); backfill(section, key, t); return defValue; }
	try { return std::stod(buffer, nullptr); }
	catch (...) {}
	return printError(section, key, defValue);
}

bool iniConfig::getBool(LPCSTR section, LPCSTR key, bool defValue)
{
	if (!get(section, key)) { backfill(section, key, defValue ? "on" : "off"); return defValue; }
	try
	{
		if (_stricmp("true", buffer) == 0 || _stricmp("on", buffer) == 0)
			return true;
		if (_stricmp("false", buffer) == 0 || _stricmp("off", buffer) == 0)
			return false;
	}
	catch (...) {}
	return printError(section, key, defValue);
}

int iniConfig::getEnum(LPCSTR section, LPCSTR key, int defValue, std::pair<int, LPCSTR> map[], int size)
{
	if (!get(section, key)) {
		for (int i = 0; i < size; i++)
			if (map[i].first == defValue) { backfill(section, key, map[i].second); break; }
		return defValue;
	}
	try
	{
		for (int i = 0; i < size; i++)
			if (_stricmp(map[i].second, buffer) == 0)
				return map[i].first;
	}
	catch (...) {}
	return printError(section, key, defValue);
}

std::vector<int> iniConfig::getInts(LPCSTR section, LPCSTR key)
{
	try
	{
		if (get(section, key, true))
		{
			std::vector<int> list;
			char *context, *token = strtok_s(buffer, ";", &context);
			while (token != nullptr)
			{
				list.push_back(std::stoi(token));
				token = strtok_s(nullptr, ";", &context);
			}
			return list;
		}
	}
	catch (...) {}
	printError(section, key, string());
	return std::vector<int>();
}

std::vector<float> iniConfig::getFloats(LPCSTR section, LPCSTR key)
{
	try
	{
		if (get(section, key, true))
		{
			std::vector<float> list;
			char *context, *token = strtok_s(buffer, ";", &context);
			while (token != nullptr)
			{
				list.push_back(std::stof(token));
				token = strtok_s(nullptr, ";", &context);
			}
			return list;
		}
	}
	catch (...) {}
	printError(section, key, string());
	return std::vector<float>();
}

void iniConfig::setStr(LPCSTR section, LPCSTR key, string value) const
{
	WritePrivateProfileStringA(section, key, (" " + value).c_str(), fileName);
}

void iniConfig::setInt(LPCSTR section, LPCSTR key, int value) const { setStr(section, key, std::to_string(value)); }
void iniConfig::setUInt(LPCSTR section, LPCSTR key, unsigned value, bool hex) const
{
	if (hex)
	{
		char buf[16];
		snprintf(buf, sizeof buf, "0x%08X", value);
		setStr(section, key, buf);
	}
	else
		setStr(section, key, std::to_string(value));
}

void iniConfig::setFloat(LPCSTR section, LPCSTR key, float value) const { setStr(section, key, std::to_string(value)); }
void iniConfig::setDouble(LPCSTR section, LPCSTR key, double value) const { setStr(section, key, std::to_string(value)); }
void iniConfig::setBool(LPCSTR section, LPCSTR key, bool value) const { setStr(section, key, value ? "on" : "off"); };
void iniConfig::setEnum(LPCSTR section, LPCSTR key, int value, std::pair<int, LPCSTR> map[], int size) const
{
	for (int i = 0; i < size; i++)
		if (map[i].first == value)
		{
			setStr(section, key, map[i].second);
			return;
		}
	setStr(section, key, std::to_string(value));
}

void iniConfig::setInts(LPCSTR section, LPCSTR key, std::vector<int> list) const
{
	string str;
	for (size_t i = 0; i < list.size(); i++)
	{
		str += std::to_string(list[i]);
		if (i < list.size() - 1)
			str += ";";
	}
	setStr(section, key, str);
}

void iniConfig::setFloats(LPCSTR section, LPCSTR key, std::vector<float> list) const
{
	string str;
	for (size_t i = 0; i < list.size(); i++)
	{
		str += std::to_string(list[i]);
		if (i < list.size() - 1)
			str += ";";
	}
	setStr(section, key, str);
}