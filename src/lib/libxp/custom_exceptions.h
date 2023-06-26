/*
	This library provides a set of exceptions duh.
*/

#pragma once

#include <exception>

static class dataref_not_found_exception : public std::exception
{
	virtual const char* what() const throw()
	{
		return "Dataref not found";
	}
} dr_not_found_ex;
