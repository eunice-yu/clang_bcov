#include <sstream>
#include <string>

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"

#include "clang/Rewrite/Core/Rewriter.h"

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolingSampleCategory("PtrFunc2");

class MyASTVisitor : public RecursiveASTVisitor<MyASTVisitor> {
	public:
		MyASTVisitor(Rewriter &R) : TheRewriter(R) {}

		bool VisitFunctionDecl(FunctionDecl *f) {
			if (f->hasBody()) {
				QualType QT = f->getReturnType();
				std::string TypeStr = QT.getAsString();

				DeclarationName DeclName = f->getNameInfo().getName();
				std::string FuncName = DeclName.getAsString();

				llvm::outs() << TypeStr << " " << FuncName << "() @ [" 
					<< f->getSourceRange().getBegin().printToString(TheRewriter.getSourceMgr())
					<< ", " << f->getSourceRange().getEnd().printToString(TheRewriter.getSourceMgr()) 
					<< "]\n" ;
			}

			return true;
		}

	private:
		Rewriter &TheRewriter;
};


class MyASTConsumer : public ASTConsumer {
	public:
  	MyASTConsumer(Rewriter &R) : Visitor(R) {}

  	bool HandleTopLevelDecl(DeclGroupRef DR) override {
    	for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b) {
      		Visitor.TraverseDecl(*b);
    	}
    	return true;
  	}

	private:
	MyASTVisitor Visitor;
};



class MyFrontendAction : public ASTFrontendAction {
	public:
	MyFrontendAction() {}

	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) override {
    	TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    	return llvm::make_unique<MyASTConsumer>(TheRewriter);
  	}

	private:
  	Rewriter TheRewriter;
};

int main(int argc, const char **argv) {
	CommonOptionsParser op(argc, argv, ToolingSampleCategory);
	ClangTool Tool(op.getCompilations(), op.getSourcePathList());
	return Tool.run(newFrontendActionFactory<MyFrontendAction>().get());
}
