#pragma once

class iniConfig
{
	CHAR buffer[1024];
	LPCSTR fileName;

	bool get(LPCSTR section, LPCSTR key, bool allowEmpty = false);
	// Дописать в пользовательский файл ключ, которого в нём нет. Подробности
	// и причина — в iniConfig.cpp, блок «ДОСЫЛКА НЕДОСТАЮЩИХ КЛЮЧЕЙ».
	void backfill(LPCSTR section, LPCSTR key, LPCSTR value) const;
	bool fileExists() const;
public:
	// Дописывать недостающие ключи с их умолчаниями. Благодаря этому
	// обновление мода НЕ требует замены ini: новые опции появятся в файле
	// сами, а выставленные значения останутся.
	bool autoBackfill = true;
	explicit iniConfig(LPCSTR fileName);

	// 85.56: путь к своему файлу. Нужен сторожу живого чтения [ranks]: он обязан
	// следить за ТЕМ ЖЕ файлом, который читает мод, а не за копией по другому
	// пути (иначе правка не подхватится, и это будет молчаливый no-op).
	LPCSTR Path() const { return fileName; }

	// 85.97: есть ли файл на диске. Публичный, потому что «конфига нет — это
	// свежая установка» теперь печатается из Initialize(), а не из конструктора
	// (см. iniConfig.cpp: конструктор обязан оставаться тривиальным).
	bool FileMissing() const;

	void removeKey(LPCSTR section, LPCSTR key) const;
	std::vector<int> getSectionInts(LPCSTR section);

	string getStr(LPCSTR section, LPCSTR key, string defValue = string());
	int getInt(LPCSTR section, LPCSTR key, int defValue);
	unsigned int getUInt(LPCSTR section, LPCSTR key, unsigned int defValue);
	float getFloat(LPCSTR section, LPCSTR key, float defValue);
	double getDouble(LPCSTR section, LPCSTR key, double defValue);
	bool getBool(LPCSTR section, LPCSTR key, bool defValue);
	int getEnum(LPCSTR section, LPCSTR key, int defValue, std::pair<int, LPCSTR> map[], int size);
	std::vector<int> getInts(LPCSTR section, LPCSTR key);
	std::vector<float> getFloats(LPCSTR section, LPCSTR key);

	void setStr(LPCSTR section, LPCSTR key, string value) const;
	void setInt(LPCSTR section, LPCSTR key, int value) const;
	void setUInt(LPCSTR section, LPCSTR key, unsigned int value, bool hex = false) const;
	void setFloat(LPCSTR section, LPCSTR key, float value) const;
	void setDouble(LPCSTR section, LPCSTR key, double value) const;
	void setBool(LPCSTR section, LPCSTR key, bool value) const;
	void setEnum(LPCSTR section, LPCSTR key, int value, std::pair<int, LPCSTR> map[], int size) const;
	void setInts(LPCSTR section, LPCSTR key, std::vector<int> list) const;
	void setFloats(LPCSTR section, LPCSTR key, std::vector<float> list) const;
};