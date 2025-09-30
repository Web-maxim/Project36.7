// User.h
#pragma once
// Безопасное хранение хеша — std::array 
#include "sha1.h"
#include <string>
#include <array>
#include <cstring> 
using namespace std;

class User {
public:
	const array<uint, 5>& get_hash() const;  
	User(const string& _name, const string& _login, const uint* pass);
	~User() = default;

	bool prov(const string& pass);

	string name;


private:
	string login;
	array<uint, 5> pass_hash{}; // было uint* с new[]/delete[]
};