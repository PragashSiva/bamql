#include "barf.hpp"

// vim: set ts=2 sw=2 tw=0 :

namespace barf {
extern llvm::Module *define_runtime(llvm::Module *module);

llvm::Value *ast_node::generate_index(llvm::Module *module,
																			llvm::IRBuilder<> &builder,
																			llvm::Value *read,
																			llvm::Value *header) {
	return llvm::ConstantInt::getTrue(llvm::getGlobalContext());
}

llvm::Function *ast_node::create_function(llvm::Module *module,
																					llvm::StringRef name,
																					llvm::StringRef param_name,
																					llvm::Type *param_type,
																					barf::generate_member member) {
	auto func = llvm::cast<llvm::Function>(module->getOrInsertFunction(
			name,
			llvm::Type::getInt1Ty(llvm::getGlobalContext()),
			llvm::PointerType::get(barf::getBamHeaderType(module), 0),
			param_type,
			nullptr));

	auto entry =
			llvm::BasicBlock::Create(llvm::getGlobalContext(), "entry", func);
	llvm::IRBuilder<> builder(entry);
	auto args = func->arg_begin();
	auto header_value = args++;
	header_value->setName("header");
	auto param_value = args++;
	param_value->setName(param_name);
	builder.CreateRet(
			(this->*member)(module, builder, param_value, header_value));

	return func;
}

llvm::Function *ast_node::create_filter_function(llvm::Module *module,
																								 llvm::StringRef name) {
	return create_function(module,
												 name,
												 "read",
												 llvm::PointerType::get(barf::getBamType(module), 0),
												 &barf::ast_node::generate);
}

llvm::Function *ast_node::create_index_function(llvm::Module *module,
																								llvm::StringRef name) {
	return create_function(module,
												 name,
												 "tid",
												 llvm::Type::getInt32Ty(llvm::getGlobalContext()),
												 &barf::ast_node::generate_index);
}

llvm::Type *getRuntimeType(llvm::Module *module, llvm::StringRef name) {
	auto struct_ty = module->getTypeByName(name);
	if (struct_ty == nullptr) {
		define_runtime(module);
		struct_ty = module->getTypeByName(name);
	}
	return struct_ty;
}

llvm::Type *getBamType(llvm::Module *module) {
	return getRuntimeType(module, "struct.bam1_t");
}

llvm::Type *getBamHeaderType(llvm::Module *module) {
	return getRuntimeType(module, "struct.bam_hdr_t");
}

llvm::Value *createString(llvm::Module *module, std::string str) {
	auto array =
			llvm::ConstantDataArray::getString(llvm::getGlobalContext(), str);
	auto global_variable = new llvm::GlobalVariable(
			*module,
			llvm::ArrayType::get(llvm::Type::getInt8Ty(llvm::getGlobalContext()),
													 str.length() + 1),
			true,
			llvm::GlobalValue::PrivateLinkage,
			0,
			".str");
	global_variable->setAlignment(1);
	global_variable->setInitializer(array);
	auto zero = llvm::ConstantInt::get(
			llvm::Type::getInt8Ty(llvm::getGlobalContext()), 0);
	std::vector<llvm::Value *> indicies;
	indicies.push_back(zero);
	indicies.push_back(zero);
	return llvm::ConstantExpr::getGetElementPtr(global_variable, indicies);
}

}