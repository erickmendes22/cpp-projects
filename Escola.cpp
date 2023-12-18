#include <iostream>
using namespace std;
#include <vector>;
class Aluno  {
private:
    string nome;
    float nota;
public:
    float getnota() {
        return this->nota;
    }
    string getnome() {
        return this->nome;
    }
    Aluno(string nome, float nota) {
        this->nome = nome;
        try {
            this->nota = nota;
            if (nota > 10.0 || nota < 0.0) {
                exception nota;
            }
        }
        catch(exception nota){
           std::cout<<"nota invalida";
        }
    }
};
class Turma {
private:
    vector<Aluno> alunos;
    float mediaNotas;
    float notaAluno;
    float totalNota;
    int numerodeAlunos;
    string nome;
    string nomeSala="Nome padrao";
public:
    void addAluno(Aluno aluno) {
        if (alunos.size() == 0) {
            totalNota = 0.0;
            numerodeAlunos = 0;
        }
        alunos.push_back(aluno);
        numerodeAlunos++;
        notaAluno=aluno.getnota();
        atualizaMedia();
    }
    void atualizaMedia() {
        mediaNotas = 0;
        totalNota += notaAluno;
        mediaNotas = totalNota / (float)numerodeAlunos;
    }
    void imprimeSalaInfo() {
        std::cout << "Nome do aluno:" << "      " << "Nota do aluno" << endl;
        for (Aluno alunox : alunos) {
            notaAluno = alunox.getnota();
            nome = alunox.getnome();
            std::cout << nome << "                  " << notaAluno<< endl;

        }
        std::cout << "O número de alunos na sala e" << " " << numerodeAlunos<< " e sua media e " << mediaNotas<< endl;

    }
    float mediaSala() {
        return mediaNotas;
    }
    void getnomesala() {
        std::cout<<nomeSala;
    }
    void setNomeSala(string nomeSala) {
        this->nomeSala = nomeSala;
    }
};
int main()
{
    Aluno heloisa = Aluno("Heloisa", 7.5);
    Aluno joao = Aluno("Joao", 5.0);
    Aluno maria = Aluno("Maria", 5.6);
    Aluno augusto = Aluno("Augusto", 6.0);
    Aluno joaquim = Aluno("Joaquim", 8.3);
    Aluno leticia = Aluno("Leticia", 10.0);
    float notaHeloisa = heloisa.getnota();
    cout << "A nota de heloisa e: " << notaHeloisa<<endl;
    Turma primeiroA;
    primeiroA.setNomeSala("Primeiro A");
    primeiroA.addAluno(heloisa);
    primeiroA.addAluno(joao);
    primeiroA.addAluno(maria);
    primeiroA.addAluno(augusto);
    primeiroA.addAluno(joaquim);
    primeiroA.addAluno(leticia);
    primeiroA.imprimeSalaInfo();

}

