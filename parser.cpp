#include <iostream>
#include "barf.hpp"

// vim: set ts=2 sw=2 tw=0 :

namespace barf {

static std::shared_ptr<ast_node> parse_conditional(const std::string &input,
																									 size_t &index,
																									 predicate_map predicates);

/**
 * Handle terminal operators (final step in the recursive descent)
 */
static std::shared_ptr<ast_node> parse_terminal(const std::string &input,
																								size_t &index,
																								predicate_map predicates) {

	parse_space(input, index);
	if (index >= input.length()) {
		throw parse_error(index, "Reached end of input before completing parsing.");
	}
	if (input[index] == '!') {
		index++;
		return std::make_shared<not_node>(parse_terminal(input, index, predicates));
	}
	if (input[index] == '(') {
		index++;
		size_t brace_index = index;
		auto node = parse_conditional(input, index, predicates);
		parse_space(input, index);
		if (index < input.length() && input[index] == ')') {
			index++;
			return node;
		} else {
			throw parse_error(brace_index,
												"Open brace has no matching closing brace.");
		}
	}
	size_t start = index;
	while (
			index < input.length() &&
			(input[index] >= 'a' && input[index] <= 'z' ||
			 ((index - start) > 0) && (input[index] == '_' || input[index] == '?' ||
																 input[index] >= '0' && input[index] <= '9'))) {
		index++;
	}
	if (start == index) {
		throw parse_error(index, "Empty predicate.");
	}

	std::string predicate_name = input.substr(start, index - start);
	if (predicates.count(predicate_name)) {
		return predicates[predicate_name](input, index);
	} else {
		index = start;
		throw parse_error(index, "Unknown predicate.");
	}
}

/**
 * Handle and operators (third step in the recursive descent)
 */
static std::shared_ptr<ast_node> parse_and(const std::string &input,
																					 size_t &index,
																					 predicate_map predicates) {
	std::vector<std::shared_ptr<ast_node>> items;

	std::shared_ptr<ast_node> node = parse_terminal(input, index, predicates);
	parse_space(input, index);
	while (index < input.length() && input[index] == '&') {
		index++;
		items.push_back(node);
		node = parse_terminal(input, index, predicates);
		parse_space(input, index);
	}
	while (items.size() > 0) {
		node = std::make_shared<and_node>(items.back(), node);
		items.pop_back();
	}
	return node;
}

/**
 * Handle or operators (second step in the recursive descent)
 */
static std::shared_ptr<ast_node> parse_or(const std::string &input,
																					size_t &index,
																					predicate_map predicates) {
	std::vector<std::shared_ptr<ast_node>> items;

	std::shared_ptr<ast_node> node = parse_and(input, index, predicates);
	parse_space(input, index);
	while (index < input.length() && input[index] == '|') {
		index++;
		items.push_back(node);
		node = parse_and(input, index, predicates);
		parse_space(input, index);
	}
	while (items.size() > 0) {
		node = std::make_shared<or_node>(items.back(), node);
		items.pop_back();
	}
	return node;
}

/**
 * Handle conditional operators (first step in the recursive descent)
 */
static std::shared_ptr<ast_node> parse_conditional(const std::string &input,
																									 size_t &index,
																									 predicate_map predicates) {
	auto cond_part = parse_or(input, index, predicates);
	parse_space(input, index);
	if (!parse_keyword(input, index, "then")) {
		return cond_part;
	}
	auto then_part = parse_or(input, index, predicates);
	if (!parse_keyword(input, index, "else")) {
		throw parse_error(index, "Ternary operator has no else.");
	}
	auto else_part = parse_or(input, index, predicates);
	return std::make_shared<conditional_node>(cond_part, then_part, else_part);
}

/**
 * Parse a string into a syntax tree using the built-in logical operations and
 * the predicates provided.
 */
std::shared_ptr<ast_node> ast_node::parse(
		const std::string &input, predicate_map predicates) throw(parse_error) {
	size_t index = 0;
	std::shared_ptr<ast_node> node = parse_conditional(input, index, predicates);

	parse_space(input, index);
	// check string is fully consumed
	if (index != input.length()) {
		throw parse_error(index, "Junk at end of input.");
	}
	return node;
}

std::shared_ptr<barf::ast_node> barf::ast_node::parse_with_logging(
		const std::string &input, predicate_map predicates) {
	std::shared_ptr<barf::ast_node> ast;
	try {
		ast = barf::ast_node::parse(input, predicates);
	}
	catch (barf::parse_error e) {
		std::cerr << "Error: " << e.what() << std::endl << input << std::endl;
		for (auto i = 0; i < e.where(); i++) {
			std::cerr << " ";
		}
		std::cerr << "^" << std::endl;
	}
	return ast;
}
}
