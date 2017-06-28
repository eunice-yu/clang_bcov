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
#include "clang/Lex/Lexer.h"
#include <iostream>
#include <fstream>
using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolingSampleCategory("LineCov");

class MyASTVisitor : public RecursiveASTVisitor<MyASTVisitor> {
	public:
		MyASTVisitor(Rewriter &R) : TheRewriter(R) {
	    }
       
		void record(Expr* cond){
			std::stringstream probe ;
			if (cond == nullptr) return;
			if (TheRewriter.isRewritable(cond->getLocStart()) && TheRewriter.isRewritable(cond->getLocEnd())) {
				CharSourceRange expr_range = clang::CharSourceRange::getTokenRange(
						TheRewriter.getSourceMgr().getSpellingLoc(cond->getLocStart()),
						TheRewriter.getSourceMgr().getSpellingLoc(cond->getLocEnd()));
				unsigned int lnum = TheRewriter.getSourceMgr().getExpansionLineNumber(cond->getLocStart());
				unsigned int cnum = TheRewriter.getSourceMgr().getExpansionColumnNumber(cond->getLocStart());
				std::string expr = Lexer::getSourceText (expr_range, TheRewriter.getSourceMgr(), TheRewriter.getLangOpts()).str();
				expr.erase(remove_if(expr.begin(), expr.end(), isspace), expr.end());
				probe << "," << cond <<"," << lnum <<"," << cnum <<  ")";
				TheRewriter.InsertTextAfterToken(cond->getLocEnd(), probe.str()) ;
				TheRewriter.InsertTextAfter(cond->getLocStart(), "_br(") ;
				{
					std::ofstream infofile("bcov.info", std::ios_base::app);
					if (infofile.is_open())
						infofile << lnum << "\t" << cnum << "\t" <<expr<<std::endl;
					infofile.close();
				}
			}
		}

		bool VisitStmt(Stmt *s) {
			if (isa<IfStmt>(s)) {
				IfStmt * ifstmt = cast<IfStmt>(s) ;
				record(ifstmt->getCond());
			}else if (isa<WhileStmt>(s)) {
				WhileStmt * whilestmt = cast<WhileStmt>(s) ;
				record(whilestmt->getCond()) ;
			}else if(isa<ForStmt>(s)) {
				ForStmt * forstmt = cast<ForStmt>(s) ;
				record(forstmt->getCond());
			}
			return true ;
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

        void EndSourceFileAction() override {
            SourceManager &SM = TheRewriter.getSourceMgr();
            TheRewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());
        }

    private:
        Rewriter TheRewriter;
};

int main(int argc, const char **argv) {

    CommonOptionsParser op(argc, argv, ToolingSampleCategory);
    ClangTool Tool(op.getCompilations(), op.getSourcePathList());

    return Tool.run(newFrontendActionFactory<MyFrontendAction>().get());
}
