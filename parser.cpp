#include <string>
#include <fstream>
#include <streambuf>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "value.cpp"

using namespace asmjit;

union ProgramValue
{
    int64_t integer;
    bool boolean;
    char *str;
    std::vector<std::unique_ptr<ProgramData>> *children;
};

enum ProgramType
{
    TYPE_INTEGER,
    TYPE_BOOLEAN,
    TYPE_STR,
    TYPE_IDENTIFIER,
    TYPE_INDEX,
    TYPE_FUNCTION,
    TYPE_FUNCTION_DEF,

    TYPE_ASSIGNMENT,
    TYPE_RETURN,
    TYPE_BLOCK,
    TYPE_IF,
    TYPE_WHILE,

    TYPE_EQUALITY,
    TYPE_NOTEQUALITY,
    TYPE_LT,

    TYPE_MULT,
    TYPE_DIV,
    TYPE_ADD,
    TYPE_SUB,
};

struct ProgramData
{
    ProgramType type;
    ProgramValue value;
};

struct ParserResult
{
    bool success;
    std::string remainder;
    std::unique_ptr<ProgramData> data;
};

ParserResult failure() 
{
    return {false, "", nullptr};
}

bool is_letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); 
}

bool is_number(char c)
{
    return (c >= '0' && c <= '9'); 
}

bool is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void eat_whitespace(std::string &program)
{
    while (!program.empty() && is_whitespace(program.at(0)))
    {
        program.erase(0, 1);
    }
}

char* strdup(const char* str)
{
      char* newstr = (char*) malloc( strlen( str) + 1);

      if (newstr) {
          strcpy( newstr, str);
      }

      return newstr;
}

ParserResult success(std::string program, ProgramData &data) 
{
    eat_whitespace(program);
    return {true, program, std::make_unique<ProgramData>(data)};
}

std::function<ParserResult(std::string)> any(std::vector<std::function<ParserResult(std::string)>> parsers)
{
    return [=](std::string program)->ParserResult {
        for (auto it = parsers.begin(); it != parsers.end(); ++it)
        {
            ParserResult result = (*it)(program);
            if (result.success)
            {
                return result;
            }
        }

        return failure();
    };
}

std::function<ParserResult(std::string)> match(std::string match_str)
{
    return [=](std::string program)->ParserResult {
        if (program.find(match_str) != 0)
            return failure();

        ProgramData data = { TYPE_STR, { .str = strdup(match_str.c_str()) } };
        return success(program.substr(match_str.size()), data);
    };
}

std::function<std::vector<ParserResult>(std::string)> seq(std::vector<std::function<ParserResult(std::string)>> parsers)
{
    return [=](std::string program)->std::vector<ParserResult> {
        std::vector<ParserResult> results;

        for (auto it = parsers.begin(); it != parsers.end(); ++it)
        {
            ParserResult result = (*it)(program);
            if (!result.success)
            {
                return {};
            }
            else
            {
                program = result.remainder;
                results.push_back(std::move(result));
            }
        }

        return results;
    };
}

ParserResult expression(std::string program);
ParserResult index(std::string program, ParserResult result);

ParserResult function(std::string program, ParserResult caller)
{
    ParserResult res = match("(")(program);

    if (!res.success)
        return caller;

    program = res.remainder;

    auto *children = new std::vector<std::unique_ptr<ProgramData>>();

    ParserResult result = expression(program);
    while (result.success)
    {
        children->push_back(std::move(result.data));
        program = result.remainder;

        result = match(",")(program);

        if (!result.success)
            break;

        result = expression(result.remainder);
    }

    res = match(")")(program);
    if (!res.success)
        return caller;

    children->insert(children->begin(), std::move(caller.data));

    ProgramData data = { TYPE_FUNCTION, { .children = children } };
    auto s = success(res.remainder, data);
    return index(s.remainder, std::move(s));
}

ParserResult identifier(std::string program);

ParserResult index(std::string program, ParserResult result)
{
    if (program.empty() || program.at(0) != '.')
        return result;
    program.erase(0, 1);

    ParserResult iden = identifier(program);
    if (!iden.success)
        return result;

    auto *children = new std::vector<std::unique_ptr<ProgramData>>();
    children->push_back(std::move(result.data));
    children->push_back(std::move(iden.data));

    ProgramData data = { TYPE_INDEX, { .children = children } };
    return success(iden.remainder, data);
}

ParserResult identifier(std::string program)
{
    std::string id;
    while (!program.empty() && (is_letter(program.at(0)) || (!id.empty() && program.at(0) == '_')))
    {
        id += program.at(0);
        program.erase(0, 1);
    }

    if (id.empty())
        return failure();

    ProgramData data = { TYPE_IDENTIFIER, { .str = strdup(id.c_str()) } };
    auto s = success(program, data);
    return index(s.remainder, std::move(s));
}

ParserResult number(std::string program)
{
    std::string id;
    if (!program.empty() && program.at(0) == '-')
    {
        id += program.at(0);
        program.erase(0, 1);
    }

    while (!program.empty() && is_number(program.at(0)))
    {
        id += program.at(0);
        program.erase(0, 1);
    }

    if (id.empty() || id == "-")
        return failure();

    ProgramData data = { TYPE_INTEGER, { .integer = std::atoi(id.c_str()) } };
    return success(program, data);
}

ParserResult addop(std::string program);

ParserResult atom(std::string program)
{
    ParserResult result = any({number, identifier})(program);
    if (result.success)
    {
        while (true) {
            auto len = result.remainder.size();
            result = function(result.remainder, std::move(result));
            if (len == result.remainder.size())
                return result;
        }
    }

    std::vector<ParserResult> results = seq({match("("), addop, match(")")})(program);

    if (results.empty())
        return failure();

    results[1].remainder = results[2].remainder;
    return function(results[1].remainder, std::move(results[1]));
}

ParserResult term(std::string program)
{
    ParserResult result = atom(program);
    if (!result.success)
        return failure();

    while (true) {
        ParserResult op = any({match("*"), match("/")})(result.remainder);

        if (!op.success)
            break;

        ParserResult rh = atom(op.remainder);

        if (!rh.success)
            return failure();

        auto *children = new std::vector<std::unique_ptr<ProgramData>>();
        children->push_back(std::move(result.data));
        children->push_back(std::move(rh.data));
        ProgramData data = { strcmp(op.data->value.str, "*") == 0 ? TYPE_MULT : TYPE_DIV, { .children = children } };
        result = success(rh.remainder, data);
    }

    return result;
}

ParserResult addop(std::string program)
{
    ParserResult result = term(program);
    if (!result.success)
        return failure();

    while (true) {
        ParserResult op = any({match("+"), match("-")})(result.remainder);

        if (!op.success)
            break;

        ParserResult rh = term(op.remainder);

        if (!rh.success)
            return failure();

        auto *children = new std::vector<std::unique_ptr<ProgramData>>();
        children->push_back(std::move(result.data));
        children->push_back(std::move(rh.data));
        ProgramData data = { strcmp(op.data->value.str, "+") == 0 ? TYPE_ADD : TYPE_SUB, { .children = children } };
        result = success(rh.remainder, data);
    }

    return result;
}

ParserResult noncompare_expression(std::string program)
{
    return any({addop})(program);
}

ParserResult equality(std::string program)
{
    std::vector<ParserResult> results = seq({noncompare_expression, any({match("=="), match("!="), match("<")}), noncompare_expression})(program);

    if (results.empty())
        return failure();

    auto *children = new std::vector<std::unique_ptr<ProgramData>>();
    children->push_back(std::move(results[0].data));
    children->push_back(std::move(results[2].data));

    auto comp = results[1].data->value.str;
    ProgramType type;
    if (strcmp(comp, "==") == 0)
        type = TYPE_EQUALITY;
    else if (strcmp(comp, "!=") == 0)
        type = TYPE_NOTEQUALITY;
    else if (strcmp(comp, "<") == 0)
        type = TYPE_LT;

    ProgramData data = {type, { .children = children } };
    return success(results[2].remainder, data);
}

ParserResult function_definition(std::string program);

ParserResult expression(std::string program)
{
    return any({function_definition, equality, noncompare_expression})(program);
}

ParserResult assignment(std::string program)
{
    std::vector<ParserResult> results = seq({identifier, match("="), expression})(program);

    if (results.empty())
        return failure();

    auto *children = new std::vector<std::unique_ptr<ProgramData>>();
    children->push_back(std::move(results[0].data));
    children->push_back(std::move(results[2].data));

    ProgramData data = { TYPE_ASSIGNMENT, { .children = children } };
    return success(results[2].remainder, data);
}

ParserResult block(std::string program);

ParserResult function_definition(std::string program)
{
    std::vector<ParserResult> results = seq({match("function"), match("("), match(")"), match("{"), block, match("}")})(program);

    if (results.empty())
        return failure();

    auto *children = new std::vector<std::unique_ptr<ProgramData>>();
    children->push_back(std::move(results[4].data));

    program = results[results.size()-1].remainder;

    ProgramData data = { TYPE_FUNCTION_DEF, { .children = children } };
    return success(program, data);
}

ParserResult if_statement(std::string program)
{
    std::vector<ParserResult> results = seq({match("if"), match("("), expression, match(")"), match("{"), block, match("}")})(program);

    if (results.empty())
        return failure();

    auto *children = new std::vector<std::unique_ptr<ProgramData>>();
    children->push_back(std::move(results[2].data));
    children->push_back(std::move(results[5].data));

    program = results[results.size()-1].remainder;
    // std::vector<ParserResult> results = seq({match("else")})(program);

    ParserResult res = match("else")(program);

    if (res.success)
    {
        program = res.remainder;

        res = match("{")(program);
        if (res.success)
        {
            program = res.remainder;
            ParserResult else_block = block(program);

            if (!else_block.success)
                return failure();

            program = else_block.remainder;
            res = match("}")(program);

            if (!res.success)
                return failure();

            program = res.remainder;
            children->push_back(std::move(else_block.data));
        } 
        else
        {
            res = if_statement(program);
            if (!res.success)
                return failure();

            program = res.remainder;
            children->push_back(std::move(res.data));
        }
    } 
    else
    {
        children->push_back(nullptr);
    }

    ProgramData data = { TYPE_IF, { .children = children } };
    return success(program, data);
}

ParserResult while_loop(std::string program)
{
    std::vector<ParserResult> results = seq({match("while"), match("("), expression, match(")"), match("{"), block, match("}")})(program);

    if (results.empty())
        return failure();

    auto *children = new std::vector<std::unique_ptr<ProgramData>>();
    children->push_back(std::move(results[2].data));
    children->push_back(std::move(results[5].data));

    ProgramData data = { TYPE_WHILE, { .children = children } };
    return success(results[results.size()-1].remainder, data);
}

ParserResult return_statement(std::string program)
{
    std::vector<ParserResult> results = seq({match("return"), expression})(program);

    if (results.empty())
        return failure();

    auto *children = new std::vector<std::unique_ptr<ProgramData>>();
    children->push_back(std::move(results[1].data));

    ProgramData data = { TYPE_RETURN, { .children = children } };
    return success(results[results.size()-1].remainder, data);
}

ParserResult statement(std::string program)
{
    return any({return_statement, assignment, if_statement, while_loop, expression})(program);   
}

ParserResult block(std::string program)
{
    auto *children = new std::vector<std::unique_ptr<ProgramData>>();

    while (true)
    {
        ParserResult result = statement(program);
        if (!result.success)
            break;

        children->push_back(std::move(result.data));
        program = result.remainder;
    }

    ProgramData data = { TYPE_BLOCK, { .children = children } };
    return success(program, data);
}
