

#include <iostream>
using namespace std;

void showMatrix(int **matrix, int n) {
    for (int i = 0;i < n;i++) {
        for (int j = 0; j < n; j++) {
            std::cout << matrix[i][j] << " ";
        }
        std::cout<<endl;
    }
}


void showMatrix(float** matrix, int n) {
    for (int i = 0;i < n;i++) {
        for (int j = 0; j < n; j++) {
            std::cout << matrix[i][j] << " ";
        }
        std::cout << endl;
    }
}

void cofactorMatrix(int **temp, int **matrix,int row,int col, int n) {
    int col_temp = 0;
    int row_temp = 0;
    for (int i = 0;i < n;i++) {
        for (int j = 0;j < n;j++) {
            if (i != row && j != col)
            {
                temp[row_temp][col_temp++] = matrix[i][j];
                if (col_temp == n - 1) {
                    col_temp = 0;
                    row_temp++;
                }

            }
        }
    }
}

int determinantMatrix(int** matrix, int n) {
    int det = 0;
    if (n == 1)
        return matrix[0][0];
    int signal = 1;
    int** temp = new int* [n];
    for (int i = 0;i < n;i++) {
        temp[i] = new int[n];
    }
    for (int i = 0; i < n;i++) {
        cofactorMatrix(temp, matrix, 0, i, n);
        det += signal * matrix[0][i] * determinantMatrix(temp, n - 1);
        signal = -1 * signal;
    }
    return det;
}

int **makematrixAdjunt(int** matrix, int n) {
    int** temp = new int* [n];
    for (int i = 0;i < n;i++) {
        temp[i] = new int[n];
    }
    int signal = -1;
    int** adjunt= new int* [n];
    for (int i = 0;i < n;i++) {
        adjunt[i] = new int[n];
    }
    int det;
    for (int i = 0; i < n;i++) {
        for (int j = 0; j < n;j++) {
            cofactorMatrix(temp, matrix, i,j, n);
            signal = -signal;
            det = determinantMatrix(temp, n - 1);
            adjunt[i][j] = det * signal;
        }
    }
    return adjunt;
}

void matrixinverse(int** matrix, int n) {
    try {
        int** adjunt = new int* [n];
        for (int i = 0;i < n;i++) {
            adjunt[i] = new int[n];
        }
        adjunt = makematrixAdjunt(matrix, n);
        int determinant = determinantMatrix(matrix, n);
        int error = 500;
        if (determinant == 0) {
            throw(error);
        }
            
        float** inverse = new float* [n];
        for (int i = 0;i < n;i++) {
            inverse[i] = new float[n];
        }
        for (int i = 0; i < n;i++) {
            for (int j = 0; j < n;j++) {
                inverse[i][j] = (float)adjunt[i][j] / (float)determinant;
            }
        }
        showMatrix(inverse, n);
    }
    catch (int error) {
        cout << "Matrix does not have inverse"<<endl;
        cout << "Error number" << error << endl;
    }
    
}
int validateIntInput(int x) {
    while (!(cin >> x)) {

        cin.clear();

        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        cout << "Input inválido, ele precisar um inteiro, tente novamente:";
    }
    return x;
}

int main()
{  
    int n=0;
    while (n <=0) {
        cout << "Digite a dimensao da matrix:" << endl;
        n = validateIntInput(n);
        if (n<=0) {
            cout << "A dimensao da matrix precisa ser maior que 0, tente novamente" << endl;
        }
    }
    int **matrix= new int*[n];
    for (int i = 0;i < n;i++) {
        matrix[i] = new int[n];
    }
    int element=0;
    for (int i = 0;i < n;i++) {
        for (int j = 0;j < n;j++) {
            cout << "Digite o elemento da matrix da linha:" << i << " coluna:" << j << endl;
            element=validateIntInput(element);
            matrix[i][j] = element;
        }

    }
    cout << "A matriz fornecinada foi:" << endl;
    showMatrix(matrix, n);
    cout << '\n';
    int determinant = determinantMatrix(matrix, n);
    std::cout<<"O determinante:" << determinant<<endl<<'\n';
    cout << "Sua matriz inversa:" << endl;
    matrixinverse(matrix, n);
}